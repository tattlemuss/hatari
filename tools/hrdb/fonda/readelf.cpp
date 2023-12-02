#include "readelf.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "dwarf_struct.h"
#include "elf_struct.h"

#define PRINTF(x)		(void)(0)
//#define PRINTF(x)		printf x
#define CHECK_RET(ret)	if ((ret)) return (ret);

namespace fonda
{
// ----------------------------------------------------------------------------
template <typename T>
	static int read_file(FILE* file, T& output)
	{
		int bytes = fread(&output, 1, sizeof(T), file);
		return bytes == sizeof(T) ? 0 : ERROR_READ_FILE;
	}

// ----------------------------------------------------------------------------
// Convert array of uint8_t values from the given endianness 
template <class T>
	static uint64_t conv_endian(const T& elf_obj, const uint8_t data_mode)
{
	uint64_t acc = 0;
	const size_t size = sizeof(elf_obj);
	if (data_mode == ELFDATA2MSB)
	{
		for (size_t i = 0; i < size; ++i)
			acc = (acc << 8) | elf_obj[i];
	}
	else
	{
		for (size_t i = size; i > 0; --i)	// e.g from 8 to 1
			acc = (acc << 8) | elf_obj[i - 1];
	}
	return acc;
}

// ----------------------------------------------------------------------------
// buffer_access -- Bounded access to a block of memory
class buffer_access
{
public:
	buffer_access() :
		m_pData(nullptr),
		m_length(0),
		m_pos(0),
		m_errored(false)
	{}

	buffer_access(const uint8_t* pData, uint64_t length) :
		m_pData(pData),
		m_length(length),
		m_pos(0),
		m_errored(false)
	{}

	// Copy bytes into the buffer
	// Returns 0 for success, 1 for failure
	int read(uint8_t* data, int count)
	{
		if (m_pos + count > m_length)
			return set_errored(data, count);
		for (int i = 0; i < count; ++i)
			*data++ = m_pData[m_pos++];
		return 0;
	}

	// Templated read of a single object
	template<typename T>
		int read(T& data)
		{
			return read((uint8_t*)&data, sizeof(T));
		}

	int read_null_term_string(std::string& result)
	{
		const uint8_t* start = get_data();
		uint8_t val;
		int ret = 0;
		do
		{
			ret |= read(val);
		} while (val);
		result = std::string((char*)start);
		return ret;
	}

	int set(uint64_t pos)
	{
		m_pos = pos;
		if (m_pos > m_length)
			return set_errored(nullptr, 0);
		return 0;
	}

	const uint8_t* get_data() const 	{ return m_pData + m_pos; }
	uint64_t get_pos() const 			{ return m_pos;	}
	bool errored() const				{ return m_errored; }

private:
	int set_errored(uint8_t* data, int count)
	{
		if (data)
			memset(data, 0, count);
		m_errored = true;
		return 1;
	}
	const uint8_t*  m_pData;
	uint64_t 		m_length;
	uint64_t		m_pos;
	bool			m_errored;
};

// ----------------------------------------------------------------------------
// loaded_chunk - Represents some loaded data block (e.g. an ELF section, or
// some ELF header data), and a buffer_reader to access it.
struct loaded_chunk
{
	loaded_chunk() : data(nullptr) {}
	~loaded_chunk() { delete [] data; }

	void reset()
	{
		delete [] data;
		data = nullptr;
		buffer = buffer_access(0, 0);
	}

	// Try to load the relevant block of data
	int load(FILE* file, size_t offset, size_t size)
	{
		reset();
		data = new uint8_t[size];
		fseek(file, offset, SEEK_SET);
		size_t bytes = fread(data, 1, size, file);
		if (bytes != size)
		{
			reset();	// free bytes
			return ERROR_READ_FILE;
		}
		buffer = buffer_access(data, size);
		return 0;
	}

	uint8_t*		  data;
	buffer_access 	  buffer;
};

// ----------------------------------------------------------------------------
// element_reader - reads atoms from the elf file, understanding endian-ness and 
// 32/64 bit modes. Uses a buffer_access object to read data.
// This class can be copied to allow random access around a chunk of buffer.
class element_reader
{
public:
	element_reader(const buffer_access& _reader, uint8_t _data_mode, uint8_t _size_mode) :
		buffer(_reader),
		data_mode(_data_mode),
		size_mode(_size_mode)
	{
		// Always reset our copy of the buffer
		buffer.set(0);
	}
	// ----------------------------------------------------------------------------
	uint32_t readU8()
	{
		uint8_t data; buffer.read(data); return data;
	}
	// ----------------------------------------------------------------------------
	uint32_t readU16()
	{
		Elf16 data; buffer.read(data); return conv_endian(data.data, data_mode);
	}
	// ----------------------------------------------------------------------------
	uint32_t readU32()
	{
		Elf32 data; buffer.read(data); return conv_endian(data.data, data_mode);
	}
	// ----------------------------------------------------------------------------
	uint64_t readU64()
	{
		Elf64 data; buffer.read(data); return conv_endian(data.data, data_mode);
	}
	// ----------------------------------------------------------------------------
	// Read 32 or 64 bit pointer, depending on the file size mode
	uint64_t readAddress()
	{
		if (size_mode == ELFCLASS32)
			return readU32();
		assert(size_mode == ELFCLASS64);
		return readU64();
	}
	// ----------------------------------------------------------------------------
	uint64_t readULEB128()
	{
		// Appendix C -- Variable Length Data: Encoding/Decoding
		uint64_t result = 0;
		uint32_t shift = 0;
		uint8_t v = 0;
		while(true)
		{
			v = readU8();
			result |= uint64_t(v & 0x7f) << shift;
			if (!(v & 0x80))
				break;
			shift += 7;
		}
		return result;
	}
	// ----------------------------------------------------------------------------
	int64_t readSLEB128()
	{
		// Appendix C -- Variable Length Data: Encoding/Decoding
		int64_t result = 0;
		int32_t shift = 0;
		const int32_t size = 64;
		uint8_t v = 0;
		while(true)
		{
			v = readU8();
			result |= uint64_t(v & 0x7f) << shift;
			shift += 7;

			/* sign bit of byte is second high order bit (0x40) */
			if (!(v & 0x80))
				break;
		}
		if ((shift <size) && (v & 0x40))
			result |= -(1 << shift);
		return result;
	}
	// ----------------------------------------------------------------------------
	// Reads either a 32-bit or 64-bit offset size, depending on the DWARF unit's
	// encoding. Also returns if this was a 32 or 64 bit DWARF
	uint64_t read_ptrsize(bool& is64bit)
	{
		// Section 7.4 "32-Bit and 64-Bit DWARF Formats"
		uint32_t val1 = readU32();
		if (val1 == 0xffffffff)
		{
			is64bit = true;
			return readU64();
		}
		is64bit = false;
		return val1;
	}
	uint64_t readU32or64(bool is64bit)
	{
		if (is64bit)
			return readU64();
		return readU32();
	}

	// ----------------------------------------------------------------------------
	std::string read_null_term_string()
	{
		std::string res;
		buffer.read_null_term_string(res);
		return res;
	}

	// ----------------------------------------------------------------------------
	int set(uint64_t pos)
	{
		return buffer.set(pos);
	}

	// ----------------------------------------------------------------------------
	uint64_t get_pos() const
	{
		return buffer.get_pos();
	}

	bool errored() const { return buffer.errored(); }
private:
	buffer_access buffer;
	uint8_t	data_mode;
	uint8_t size_mode;
};

// ----------------------------------------------------------------------------
// All the (internal) info about an elf section that we store.
struct elf_section_int
{
	elf_section_int() : 
		section_id(0),
		chunk(),
		is_loaded(false)
	{}
	
	std::string		name_string;		// e.g. ".debug_info"
	uint32_t 		section_id;
	loaded_chunk	chunk;
	bool			is_loaded = false;	// whether "chunk" is valid

	// These are from the section header information in the elf
	uint32_t 		sh_name;			// offset in the section-name section
	uint32_t 		sh_type;			// one of SHT_*
	uint64_t 		sh_flags;			// bitfield of SHF_*
	uint64_t 		sh_addr;
	uint64_t 		sh_offset;			// position of the section in the elf file
	uint64_t 		sh_size;			// size in bytes
	uint32_t 		sh_link;			// semantically-linked section number (e.g for string block)
	uint32_t 		sh_info;			//
	uint64_t 		sh_addralign;
	uint64_t 		sh_entsize;
};

// ----------------------------------------------------------------------------
struct elf
{
	Elf_Ident 	ident;
	FILE*		file;
	elf_section_int* sections;		// flat array of section info

	int load_section(size_t section_num);

	// Return a reader object, potentially loading the section if necessary
	element_reader create_reader(uint32_t section_num);

	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;		// size of each section header
	uint16_t e_shnum;			// number of section headers (and hence, sections)
	uint16_t e_shstrndx;		// which section contains strings for the names of section headers
};

// ----------------------------------------------------------------------------
struct content_line
{
	std::string path;
	int directory_index;
	uint64_t timestamp;
	uint64_t size;
	uint64_t mdd5;
};

struct content_desc
{
	uint8_t type;
	uint8_t form;
};

// ----------------------------------------------------------------------------
struct line_state_machine
{
	uint64_t address;
	uint32_t file_index;
	int32_t line;
	uint32_t column;
	bool is_stmt;
	bool basic_block;
	bool end_sequence;
	bool prologue_end;
	bool epilogue_begin;
	uint32_t isa;
	uint64_t discriminator;
};

// ----------------------------------------------------------------------------
int elf::load_section(size_t section_num)
{
	if (section_num >= this->e_shnum)
		return ERROR_INVALID_SECTION;
	int ret = 0;
	elf_section_int& section = sections[section_num];
	if (section.is_loaded)
		return 0;
	ret = section.chunk.load(file, section.sh_offset, section.sh_size);
	if (ret == 0)
		section.is_loaded = true;
	return ret;
}

// ----------------------------------------------------------------------------
element_reader elf::create_reader(uint32_t section_num)
{
	load_section(section_num);
	elf_section_int& section = sections[section_num];
	return element_reader(section.chunk.buffer, ident.ei_data, ident.ei_class);
}

// ----------------------------------------------------------------------------
static elf_section_int* load_named_section(elf& elf, const char* name)
{
	for (size_t sectionId = 0; sectionId < elf.e_shnum; ++sectionId)
	{
		elf_section_int& s = elf.sections[sectionId];
		if (s.name_string == name)
		{
			if (elf.load_section(sectionId))
				return nullptr;
			return &s;
		}
	}
	return nullptr;
}

// ----------------------------------------------------------------------------
static std::string read_debug_string(elf& elf, const char* section_name, uint64_t offset)
{
	// This will load the section if necessary
	elf_section_int* s = load_named_section(elf, section_name);
	if (!s)
		return "";

	// Create a new reader object for the section
	element_reader tmp_read = elf.create_reader(s->section_id);
	tmp_read.set(offset);
	return tmp_read.read_null_term_string();
}

// ----------------------------------------------------------------------------
static void reset(line_state_machine& sm)
{
	sm.line = 1;
	sm.file_index = 1;
	sm.address = 0;
	sm.column = 0;
	sm.is_stmt = false;
	sm.basic_block = false;
	sm.end_sequence = false;
	sm.prologue_end = false;
	sm.epilogue_begin = false;
	sm.isa = 0;
	sm.discriminator = 0;
};

// ----------------------------------------------------------------------------
int read_content_line(content_line& content, element_reader& eread, elf& elf_data,
	const std::vector<content_desc>& descs, bool is64Bit)
{
	for (size_t fieldId = 0; fieldId < descs.size(); ++fieldId)
	{
		const content_desc& desc = descs[fieldId];
		switch (desc.type)
		{
			case DW_LNCT_directory_index:
			{
				switch (desc.form)
				{
					case DW_FORM_data1:	content.directory_index = eread.readU8(); continue;
					case DW_FORM_data2:	content.directory_index = eread.readU16(); continue;
					case DW_FORM_udata:	content.directory_index = eread.readULEB128(); continue;
					default: return ERROR_DWARF_UNKNOWN_CONTENT_FORM;
				}
				break;
			}
			case DW_LNCT_path:
			{
				uint64_t offset;
				switch (desc.form)
				{
					case DW_FORM_string:	content.path = eread.read_null_term_string(); continue;
					case DW_FORM_line_strp:
						offset = eread.readU32or64(is64Bit);
						content.path = read_debug_string(elf_data, ".debug_line_str", offset);
						continue;
					case DW_FORM_strp:		content.path = eread.readULEB128(); continue;
						offset = eread.readU32or64(is64Bit);
						content.path = read_debug_string(elf_data, ".debug_str", offset);
						continue;
					// NO CHECK
					//case DW_FORM_strp_sup:	content.path = eread.readULEB128(); continue;
					//	offset = eread.readU32or64(is64Bit);
					//	content.path = ReadDebugString(elf_data, ".debug_line_str", offset);
					//	continue;
					default: return ERROR_DWARF_UNKNOWN_CONTENT_FORM;
				}
				break;
			}
			default:
				return ERROR_DWARF_UNKNOWN_CONTENT_TYPE;
		}
	}
	return OK;
}

// ----------------------------------------------------------------------------
static void add_codepoint(const line_state_machine& sm, compilation_unit& unit)
{
	code_point p;
	p.address = sm.address;
	p.column = sm.column;
	p.line = sm.line;
	p.file_index = sm.file_index;
	unit.points.push_back(p);
}

// ----------------------------------------------------------------------------
static int parse_section_debug_info(elf& elf, elf_section_int& section)
{
	element_reader eread = elf.create_reader(section.section_id);
	bool is64Bit;
	uint32_t unit_length = eread.read_ptrsize(is64Bit);
	uint16_t dwarf_version = eread.readU16();
	uint8_t unit_type = 0xff;
	uint8_t address_size = 0;
	uint64_t debug_abbrev_offset = 0;
	(void) unit_length;
	(void) unit_type;
	(void) address_size;

	if (dwarf_version <= 4)
	{
		debug_abbrev_offset = eread.readU32or64(is64Bit);
		address_size = eread.readU8();
	}
	else if (dwarf_version == 5)
	{
		unit_type = eread.readU8();
		// The following two are tranposed in v5 onwards!
		address_size = eread.readU8();
		debug_abbrev_offset = eread.readU32or64(is64Bit);
	}
	(void) debug_abbrev_offset;
	return 0;
}

// ----------------------------------------------------------------------------
static int parse_section_debug_line(elf_results& output, 
	elf& elf, const elf_section_int& section)
{
	element_reader eread = elf.create_reader(section.section_id);
	uint64_t section_end_pos = section.sh_size;
	int ret;

	// Read all the compilation units in turn
	while (1)
	{
		if (eread.get_pos() > section_end_pos)
			return ERROR_DWARF_DEBUGLINE_PARSE;

		if (eread.get_pos() == section_end_pos)
			break;

		// 6.2.4 The Line Number Program Header
		// (for each compilation unit)
		bool is64bit = false;
		uint64_t unit_length = eread.read_ptrsize(is64bit);

		uint64_t unit_start_pos = eread.get_pos();	// the start of unit doesn't include unit_length
		uint16_t line_number_version = eread.readU16();	// this is the line number version
		uint8_t address_size = 0;
		uint8_t segment_selector_size = 0;
		if (line_number_version >= 5)
			address_size = eread.readU8();
		if (line_number_version >= 5)
			segment_selector_size = eread.readU8();
		(void)address_size;
		(void)segment_selector_size;

		// This varies with 32/64 bit formats
		uint64_t header_length = eread.readU32or64(is64bit);
		uint8_t minimum_instruction_length = eread.readU8();
		uint8_t maximum_operations_per_instruction = 0;
		if (line_number_version >= 5)
			maximum_operations_per_instruction = eread.readU8();
		uint8_t default_is_stmt = eread.readU8();
		int8_t line_base = (int8_t)eread.readU8();
		uint8_t line_range = eread.readU8();
		uint8_t opcode_base = eread.readU8();
		// Suppress unused warnings
		(void) header_length;
		(void) maximum_operations_per_instruction;

		output.line_info_units.push_back(compilation_unit());
		compilation_unit& compilation_unit = output.line_info_units.back();

		// Now read a series of opcode lengths
		for (uint8_t i = 1; i < opcode_base; ++i)
		{
			uint8_t length = eread.readU8();
			PRINTF(("\topcode %u has %u args\n", i, length));
			(void) length;
		}

		compilation_unit.dirs.push_back(".");
		if (line_number_version >= 5)		// NO CHECK ??? or dwarf_version?
		{
			uint8_t directory_entry_format_count = eread.readU8();

			std::vector<content_desc> descs;
			std::vector<content_desc> fileDescs;

			for (uint8_t i = 0; i < directory_entry_format_count; ++i)
			{
				content_desc desc;
				desc.type = eread.readULEB128();
				desc.form = eread.readULEB128();
				descs.push_back(desc);
			}
			uint64_t directories_count = eread.readULEB128();
			for (uint64_t i = 0; i < directories_count; ++i)
			{
				content_line cl;
				ret = read_content_line(cl, eread, elf, descs, is64bit);
				CHECK_RET(ret)
				compilation_unit.dirs.push_back(cl.path);
			}
			uint64_t file_name_entry_format_count = eread.readULEB128();
			for (uint8_t i = 0; i < file_name_entry_format_count; ++i)
			{
				content_desc desc = {};
				desc.type = eread.readULEB128();
				desc.form = eread.readULEB128();
				fileDescs.push_back(desc);
			}

			uint64_t files_count = eread.readULEB128();
			for (uint64_t i = 0; i < files_count; ++i)
			{
				content_line cl = {};
				ret = read_content_line(cl, eread, elf, fileDescs, is64bit);
				CHECK_RET(ret)

				// Copy out the necessary bits
				compilation_unit::file file;
				file.dir_index = cl.directory_index;
				file.path = cl.path;
				file.length = 0;
				file.timestamp = 0;
				compilation_unit.files.push_back(file);
			}
		}
		else
		{
			// Original, simpler directory/file name description
			while (1)
			{
				std::string dir = eread.read_null_term_string();
				if (dir.size() == 0)
					break;
				compilation_unit.dirs.push_back(dir);
			}

			{
				compilation_unit::file f;
				f.path = "NONE";
				f.dir_index = 0;
				f.length = 0;
				f.timestamp = 0;
				compilation_unit.files.push_back(f);
			}
			while (1)
			{
				std::string file_name = eread.read_null_term_string();
				if (file_name.size() == 0)
					break;

				compilation_unit::file f;
				f.dir_index = eread.readULEB128();
				f.timestamp = eread.readULEB128();
				f.length = eread.readULEB128();
				f.path = file_name;

				compilation_unit.files.push_back(f);
			}
		}
		if (eread.errored())
			return ERROR_READ_FILE;		

		// Now the compilation units
		line_state_machine sm;
		reset(sm);
		sm.is_stmt = default_is_stmt;

		uint64_t unit_end_pos = unit_start_pos + unit_length;
		while (1)
		{
			assert(sm.line >= 1);
			if (eread.get_pos() > unit_end_pos)
				return ERROR_DWARF_DEBUGLINE_PARSE;
			if (eread.get_pos() == unit_end_pos)
				break;
			if (eread.errored())
				return ERROR_READ_FILE;

			assert(eread.get_pos() < unit_end_pos);
			uint8_t opcode0 = eread.readU8();
			PRINTF(("--- pos: 0x%x opcode0: %x\n", debug_pos, opcode0));
			if (opcode0 == 0)
			{
				// Extended opcode
				uint64_t length = eread.readULEB128();
				PRINTF(("length: %x\n", length));
				(void)length;		// should we use this for correctness checking?

				uint8_t extended_opcode = eread.readU8();
				PRINTF(("extended_opcode: %x\n", extended_opcode));

				if (extended_opcode == DW_LNE_set_address)
				{
					uint64_t addr = eread.readAddress();
					PRINTF(("DW_LNE_set_address addr: %x\n", addr));
					sm.address = addr;
				}
				else if (extended_opcode == DW_LNE_end_sequence)
				{
					PRINTF(("DW_LNE_end_sequence\n"));
					add_codepoint(sm, compilation_unit);
					reset(sm);
					sm.is_stmt = default_is_stmt;
				}
				else if (extended_opcode == DW_LNE_define_file)
				{
					PRINTF(("DW_LNE_define_file\n"));
					compilation_unit::file f;
					uint64_t dir_index = eread.readULEB128();
					f.timestamp = eread.readULEB128();
					f.length = eread.readULEB128();
					f.path = eread.read_null_term_string();
					f.dir_index = dir_index;
					PRINTF(("New file: \"%s\" dir_index: %x mod_ts: %x length: %x\n",
						f.filename, f.dir_index, f.timestamp, f.length));
					compilation_unit.files.push_back(f);
				}
				else if (extended_opcode == DW_LNE_set_discriminator)
				{
					sm.discriminator = eread.readULEB128();
					PRINTF(("DW_LNE_set_discriminator: %llx\n", sm.discriminator));
				}
				else
				{
					printf("unknown extended_opcode\n");
					printf("extended_opcode: %x\n", extended_opcode);
					return ERROR_DWARF_UNKNOWN_EXTENDED_OPCODE;
				}
			}
			else if (opcode0 == DW_LNS_advance_pc)
			{
				// NOTE: unsigned!
				uint64_t adv = eread.readULEB128();
				sm.address += adv * minimum_instruction_length;
				PRINTF(("advance PC by %d to %x\n", adv, sm.address));
			}
			else if (opcode0 == DW_LNS_advance_line)
			{
				int64_t adv = eread.readSLEB128();
				sm.line += adv;
				PRINTF(("  [0x%08x]  Advance Line by %lld to %lld\n", debug_pos, adv, sm.line));
			}
			else if (opcode0 == DW_LNS_copy)
			{
				add_codepoint(sm, compilation_unit);
			}
			else if (opcode0 == DW_LNS_set_file)
			{
				uint64_t file_index = eread.readULEB128();
				PRINTF(("Set file index to %lld\n", file_index));
				sm.file_index = file_index;
			}
			else if (opcode0 == DW_LNS_set_column)
			{
				uint64_t column = eread.readULEB128();
				PRINTF(("Set column to %lld\n", column));
				sm.column = column;
			}
			else if (opcode0 == DW_LNS_negate_stmt)
			{
				PRINTF(("Negate stat\n"));
				sm.is_stmt = !sm.is_stmt;
			}
			else if (opcode0 == DW_LNS_const_add_pc)
			{
				int32_t adjusted_opcode = (uint32_t)255 - (uint32_t)opcode_base;
				uint64_t addr_increment = uint64_t(adjusted_opcode / line_range) * minimum_instruction_length;
				PRINTF(("Advance PC by %lld\n", addr_increment));
				sm.address += addr_increment;
			}
			else if (opcode0 >= opcode_base)
			{
				// 6.2.5.1 Special Opcodes
				int32_t adjusted_opcode = (uint32_t)opcode0 - (uint32_t)opcode_base;
				uint64_t addr_increment = uint64_t(adjusted_opcode / line_range) * minimum_instruction_length;

				int32_t line_increment = line_base + (adjusted_opcode % line_range);
				PRINTF(("Special opcode: adjusted_opcode=%d line_range=%d addr_inc=%d line_inc=%d\n",
					adjusted_opcode,
					line_range,
					addr_increment, line_increment));
				sm.address += addr_increment;
				sm.line += line_increment;
				add_codepoint(sm, compilation_unit);
				sm.basic_block = false;
				sm.prologue_end = false;
				sm.epilogue_begin = false;
			}
			else
			{
				printf("unknown opcode\n");
				printf("opcode0: %x\n", opcode0);
				return ERROR_DWARF_UNKNOWN_OPCODE;
			}
		}
	}
	return 0;
}

// ----------------------------------------------------------------------------
// Templated function to read either Elf32_sym or Elf64_sym
template <typename ELF_SYMBOL>
	static int read_elf_symbol(elf_symbol& symbol, elf& elf_data, buffer_access& buffer)
{
	ELF_SYMBOL file_sym;
	if (buffer.read(file_sym) != 0)
		return ERROR_READ_FILE;

	uint8_t mode = elf_data.ident.ei_data;
	symbol.st_name       = conv_endian(file_sym.st_name, mode);
	symbol.st_value      = conv_endian(file_sym.st_value, mode);
	symbol.st_size       = conv_endian(file_sym.st_size, mode);
	symbol.st_info       = conv_endian(file_sym.st_info, mode);
	symbol.st_other      = conv_endian(file_sym.st_other, mode);
	symbol.st_shndx      = conv_endian(file_sym.st_shndx, mode);
	return OK;
}

// ----------------------------------------------------------------------------
static int parse_section_symbol(elf_results& output, 
	elf& elf, const elf_section_int& section)
{
	uint8_t data_class = elf.ident.ei_class;	// 32 bit or 64 bit
	int ret = OK;
	// Load the necessary chunks:
	// the symbol section...
	ret = elf.load_section(section.section_id);
	CHECK_RET(ret)

	// ... and the strings for the symbols.
	ret = elf.load_section(section.sh_link);
	CHECK_RET(ret)

	const elf_section_int& symbol_section = section;
	buffer_access sym_buffer = symbol_section.chunk.buffer;
	sym_buffer.set(0);

	// The linked string table is in sh_link...
	element_reader name_read = elf.create_reader(section.sh_link);
	while (sym_buffer.get_pos() < section.sh_size)
	{
		elf_symbol sym;
		if (data_class == ELFCLASS32)
			ret = read_elf_symbol<Elf32_Sym>(sym, elf, sym_buffer);
		else if (data_class == ELFCLASS64)
			ret = read_elf_symbol<Elf64_Sym>(sym, elf, sym_buffer);
		CHECK_RET(ret);

		name_read.set(sym.st_name);
		sym.name = name_read.read_null_term_string();
		if (sym.st_shndx < elf.e_shnum)
		{
			const elf_section_int& ref_section = elf.sections[sym.st_shndx];
			sym.section_type = ref_section.name_string;
		}
		else if (sym.st_shndx == SHN_ABS)
			sym.section_type = "ABS";
		else if (sym.st_shndx == SHN_COMMON)
			sym.section_type = "COMMON";

		output.symbols.push_back(sym);
	}
	return OK;
}

// ----------------------------------------------------------------------------
// Templated function to read either Elf32_hdr or Elf64_hdr
template <typename ELF_FILE_HEADER>
	static int read_elf_header(elf& elf_data, FILE* file)
{
	ELF_FILE_HEADER hdr;
	if (read_file(file, hdr) != 0)
		return ERROR_READ_FILE;

	uint8_t mode = elf_data.ident.ei_data;
	elf_data.e_type 	 = conv_endian(hdr.e_type, mode);
	elf_data.e_machine	 = conv_endian(hdr.e_machine, mode);
	elf_data.e_version	 = conv_endian(hdr.e_version, mode);
	elf_data.e_entry	 = conv_endian(hdr.e_entry, mode);
	elf_data.e_phoff	 = conv_endian(hdr.e_phoff, mode);
	elf_data.e_shoff	 = conv_endian(hdr.e_shoff, mode);
	elf_data.e_flags	 = conv_endian(hdr.e_flags, mode);
	elf_data.e_ehsize	 = conv_endian(hdr.e_ehsize, mode);
	elf_data.e_phentsize = conv_endian(hdr.e_phentsize, mode);
	elf_data.e_phnum	 = conv_endian(hdr.e_phnum, mode);
	elf_data.e_shentsize = conv_endian(hdr.e_shentsize, mode);
	elf_data.e_shnum	 = conv_endian(hdr.e_shnum, mode);
	elf_data.e_shstrndx	 = conv_endian(hdr.e_shstrndx, mode);
	return OK;
}

// ----------------------------------------------------------------------------
static int process_elf_file_internal(elf& elf_data, FILE* file, elf_results& output)
{
	output.sections.clear();
	output.line_info_units.clear();
	output.symbols.clear();

	elf_data.file = file;

	int ret = read_file(file, elf_data.ident);
	CHECK_RET(ret);

	// Check header
	static char magic[4] = { 0x7f, 'E', 'L', 'F'};
	for (size_t i = 0; i < 4; ++i)
		if (elf_data.ident.ei_magic[i] != magic[i])
			return ERROR_HEADER_MAGIC_FAIL;

	uint8_t data_class = elf_data.ident.ei_class;	// 32 bit or 64 bit

	// Read main ELF header variants
	if (data_class == ELFCLASS32)
		ret = read_elf_header<Elf32_Ehdr>(elf_data, file);
	else if (data_class == ELFCLASS64)
		ret = read_elf_header<Elf64_Ehdr>(elf_data, file);
	else
		return ERROR_UNKNOWN_CLASS;

	if (elf_data.e_version != EV_CURRENT)
		return ERROR_ELF_VERSION;

	// Load the section header data itself, into a custom block
	loaded_chunk entries_chunk;
	if (entries_chunk.load(file, elf_data.e_shoff, elf_data.e_shnum * elf_data.e_shentsize) != 0)
		return 3;
	// Create a reader for the section headers
	element_reader sh_reader(entries_chunk.buffer, elf_data.ident.ei_data, elf_data.ident.ei_class);

	// Read sections' raw information
	elf_data.sections = new elf_section_int[elf_data.e_shnum];
	for (uint32_t sectionId = 0; sectionId < elf_data.e_shnum; ++sectionId)
	{
		elf_section_int& s = elf_data.sections[sectionId];
		s.sh_name       = sh_reader.readU32();
		s.sh_type       = sh_reader.readU32();
		s.sh_flags      = sh_reader.readAddress();
		s.sh_addr       = sh_reader.readAddress();
		s.sh_offset     = sh_reader.readAddress();
		s.sh_size       = sh_reader.readAddress();
		s.sh_link       = sh_reader.readU32();
		s.sh_info       = sh_reader.readU32();
		s.sh_addralign  = sh_reader.readAddress();
		s.sh_entsize    = sh_reader.readAddress();
		s.section_id	= sectionId;
		if (sh_reader.errored())
			return 3;
	}

	// Load the section with the section's name strings in
	if (elf_data.load_section(elf_data.e_shstrndx) != 0)
		return -5;

	// Now read the section names
	element_reader name_reader = elf_data.create_reader(elf_data.e_shstrndx);
	for (uint32_t sectionId = 0; sectionId < elf_data.e_shnum; ++sectionId)
	{
		elf_section_int& s = elf_data.sections[sectionId];

		// Jump into the section with strings and read a string
		name_reader.set(s.sh_name);
		std::string section_name = name_reader.read_null_term_string();
		s.name_string = section_name;
		if (name_reader.errored())
			return ERROR_READ_FILE;

		// Copy to results now the name is known.
		elf_section result_sec = {};
		result_sec.section_id  = s.section_id;
		result_sec.name_string = s.name_string;
		result_sec.offset      = s.sh_offset;
		result_sec.size        = s.sh_size;
		result_sec.type        = s.sh_type;
		result_sec.flags       = s.sh_flags;
		result_sec.addr        = s.sh_addr;
		output.sections.push_back(result_sec);			
	}

	elf_section_int* debug_info_section = load_named_section(elf_data, ".debug_info");
	if (debug_info_section)
	{
		int ret = parse_section_debug_info(elf_data, *debug_info_section);
		CHECK_RET(ret);
	}

	const elf_section_int* debug_line_section = load_named_section(elf_data, ".debug_line");
	if (debug_line_section)
	{
		int ret = parse_section_debug_line(output, elf_data, *debug_line_section);
		CHECK_RET(ret);
	}

	for (uint32_t sectionId = 0; sectionId < elf_data.e_shnum; ++sectionId)
	{
		const elf_section_int& s = elf_data.sections[sectionId];
		// Move to section start
		if (s.sh_type == SHT_SYMTAB)
		{
			ret = parse_section_symbol(output, elf_data, s);
			CHECK_RET(ret);
		}
	}
	return OK;
}

// ----------------------------------------------------------------------------
int process_elf_file(FILE* file, elf_results& output)
{
	elf elf_data;
	elf_data.sections = nullptr;

	int ret = process_elf_file_internal(elf_data, file, output);
	delete [] elf_data.sections;
	return ret;
}

} // namespace