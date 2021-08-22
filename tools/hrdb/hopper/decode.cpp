#include <assert.h>
#include <cstddef>
#include "decode.h"

#include "instruction.h"
#include "buffer.h"

// ----------------------------------------------------------------------------
//	INSTRUCTION DECODE
// ----------------------------------------------------------------------------
// Set functions to correctly handle operand union data
void set_imm_byte(operand& op, uint8_t val)
{
	op.type = OpType::IMMEDIATE;
	op.imm.size = Size::BYTE;
	op.imm.val0 = val;
}

void set_imm_word(operand& op, uint16_t val)
{
	op.type = OpType::IMMEDIATE;
	op.imm.size = Size::WORD;
	op.imm.val0 = val;
}

void set_imm_long(operand& op, uint32_t val)
{
	op.type = OpType::IMMEDIATE;
	op.imm.size = Size::LONG;
	op.imm.val0 = val;
}

void set_dreg(operand& op, uint8_t reg)
{
	op.type = OpType::D_DIRECT;
	op.d_register.reg = reg;
}

void set_areg(operand& op, uint8_t reg)
{
	op.type = OpType::A_DIRECT;
	op.a_register.reg = reg;
}

void set_predec(operand& op, uint8_t reg)
{
	op.type = OpType::INDIRECT_PREDEC;
	op.indirect_predec.reg = reg;
}

void set_postinc(operand& op, uint8_t reg)
{
	op.type = OpType::INDIRECT_POSTINC;
	op.indirect_postinc.reg = reg;
}

// Effective Address encoding modes.
// Different instructions have different sets of EAs which are allowable to be used
// for each operand. This ID flags which ones are allowed.
enum class ea_group
{
	DATA_ALT		= 0,	// Data alterable (no A-reg, no PC-rel, no immedidate)
	DATA			= 1,	// Data, non-alterable (source)
	MEM_ALT			= 2,	// Memory, alterable (no A-reg, no PC-rel, no immedidate)
	MEM				= 3,	// Memory, non-alterable (source)
	CONTROL			= 4,
	CONTROL_MOVEM1	= 5,	// Movem to memory
	CONTROL_MOVEM2	= 6,	// Movem to reg
	ALT				= 7,	// Alterable including A-reg
	ALL				= 8,	// e.g. cmp
	COUNT
};

// Defines which instruction modes are allowed to have which EA modes
static bool mode_availability[][static_cast<int>(ea_group::COUNT)] =
{
	// DataAlt	Data	MemAlt	Mem		Ctrl	CMovem	CMovem2	Alt		All
	{	true,	true,	false,	false,	false,	false,	false,	true,	true	}, // D_DIRECT			000 regno
	{	false,	false,	false,	false,	false,	false,	false,	true,	true	}, // A_DIRECT			001 regno
	{	true,	true,	true,	true,	true,	true,	true,	true,	true	}, // INDIRECT			010 regno
	{	true,	true,	true,	true,	false,	false,	true,	true,	true	}, // INDIRECT_POSTINC	011 regno
	{	true,	true,	true,	true,	false,	true,	true,	true,	true	}, // INDIRECT_PREDEC	 100 regno
	{	true,	true,	true,	true,	true,	true,	true,	true,	true	}, // INDIRECT_DISP	   101 regno
	{	true,	true,	true,	true,	true,	true,	true,	true,	true	}, // INDIRECT_INDEX,	 110 regno
	{	true,	true,	true,	true,	true,	true,	true,	true,	true	}, // ABSOLUTE_WORD	   111 000
	{	true,	true,	true,	true,	true,	true,	true,	true,	true	}, // ABSOLUTE_LONG	   111 001  // There is a typo in the doc here
	{	false,  true,	false,	true,	true,	true,	true,	false,	true	}, // PC_DISP			 111 010
	{	false,  true,	false,	true,	true,	true,	true,	false,	true	}, // PC_DISP_INDEX	   111 011
	{	false,  true,	false,	true,	false,	false,	false,	false,	true	}, // IMMEDIATE		   111 100
	{	false,  false,	false,	false,	false,	false,	false,	false,	false	}, // INVALID		   111 100
};

// There is a consistent approach to decoding the mode bits (0-6),
// and when the mode is 7, using the register bits
// Convert these mixed bits to a range [0-12)
uint8_t decode_operand_type(uint8_t mode_bits, uint8_t reg_bits)
{
	if (mode_bits < 7)
		return mode_bits;

	if (reg_bits <= 4)
		return  7 + reg_bits;

	// Invalid!
	return 7 + 5;
}

// ----------------------------------------------------------------------------
// Split 16 bit raw displacement into signed 8-bit offset, register index and size
// e.g n(pc,d0.w) or n(a0,d0.w)
void decode_brief_extension_word(uint16_t word, int8_t& disp, uint8_t& data_reg, bool& is_long)
{
	// The offset is 8-bits
	disp = (int8_t)(word & 0xff);
	data_reg = (word >> 12) & 7;
	is_long = ((word >> 11) & 1);
}

int read_immediate(buffer_reader& buffer, operand& operand, Size size)
{
	uint16_t val16;
	uint32_t val32;
	operand.type = OpType::IMMEDIATE;
	switch (size)
	{
		case Size::BYTE:
			// byte
			if (buffer.read_word(val16))
				return 1;
			operand.imm.size = Size::BYTE;
			operand.imm.val0 = val16 & 0xff;
			break;
		case Size::WORD:
			// word
			if (buffer.read_word(val16))
				return 1;
			operand.imm.size = Size::WORD;
			operand.imm.val0 = val16;
			break;
		case Size::LONG:
			// long
			if (buffer.read_long(val32))
				return 1;
			operand.imm.size = Size::LONG;
			operand.imm.val0 = val32;
			break;
		default:
			return 1;	// error!
	}
	return 0;
}

// ----------------------------------------------------------------------------
int decode_ea(buffer_reader& buffer, operand& operand, ea_group group, uint8_t mode_bits, uint8_t reg_bits, Size size)
{
	// Convert to an operand type
	int ea_type = decode_operand_type(mode_bits, reg_bits);

	// Check EA type is valid for this instruction
	bool allow = mode_availability[ea_type][(int)group];
	if (!allow)
		return 1;

	static const OpType types[] =
	{
		// 0-6 in "mode" bits
		//                                mode
		D_DIRECT,			// Dn		  000 reg. number:Dn
		A_DIRECT,			// An		  001 reg. number:An
		INDIRECT,			// (An)	      010 reg. number:An
		INDIRECT_POSTINC,	// (An) +	  011 reg. number:An
		INDIRECT_PREDEC,	// – (An)	  100 reg. number:An
		INDIRECT_DISP,	    // (d16,An)   101 reg. number:An
		INDIRECT_INDEX,	    // (d8,An,Xn) 110 reg. number:An

		// 7 in "mode" bits, 0-4 in reg bits
		//                                  mode reg
		ABSOLUTE_WORD,      // (xxx).W      111 000
		ABSOLUTE_LONG,      // (xxx).L      111 001
		PC_DISP,		    // (d16,PC)     111 010
		PC_DISP_INDEX,      // (d8,PC,Xn)   111 011
		IMMEDIATE,          // <data>       111 100
		INVALID,
	};
	assert(ea_type <= 7 + 4 + 1);
	operand.type = types[ea_type];

	uint16_t val16;
	uint32_t val32;
	switch (operand.type)
	{
		case OpType::D_DIRECT:
			operand.d_register.reg = reg_bits;
			return 0;

		case OpType::A_DIRECT:
			// Special case: address register direct never allowed with byte
			// operations.
			if (size == Size::BYTE)
				return 1;
			operand.a_register.reg = reg_bits;
			return 0;

		case OpType::INDIRECT:
			operand.indirect.reg = reg_bits;
			return 0;

		case OpType::INDIRECT_POSTINC:
			operand.indirect_postinc.reg = reg_bits;
			return 0;

		case OpType::INDIRECT_PREDEC:
			operand.indirect_predec.reg = reg_bits;
			return 0;

		case OpType::INDIRECT_DISP:
			operand.indirect_disp.reg = reg_bits;
			if (buffer.read_word(val16))
				return 1;
			// 16-bit displacement
			operand.indirect_disp.disp = (int16_t)val16;
			return 0;

		case OpType::INDIRECT_INDEX:
			operand.indirect_index.a_reg = reg_bits;
			if (buffer.read_word(val16))
				return 1;
			decode_brief_extension_word(val16,
				operand.indirect_index.disp,
				operand.indirect_index.d_reg,
				operand.indirect_index.is_long);
			return 0;

		case OpType::ABSOLUTE_WORD:
			if (buffer.read_word(val16))
				return 1;
			operand.absolute_word.wordaddr = val16;
			return 0;

		case OpType::ABSOLUTE_LONG:
			if (buffer.read_long(val32))
				return 1;
			operand.absolute_long.longaddr = val32;
			return 0;

		case OpType::PC_DISP:
			if (buffer.read_word(val16))
				return 1;
			// 16-bit displacement
			operand.pc_disp.disp = (int16_t)val16;
			return 0;

		case OpType::PC_DISP_INDEX:
			if (buffer.read_word(val16))
				return 1;
			decode_brief_extension_word(val16,
				operand.pc_disp_index.disp,
				operand.pc_disp_index.d_reg,
				operand.pc_disp_index.is_long);
			return 0;

		case OpType::IMMEDIATE:
			return read_immediate(buffer, operand, size);

		default:
			break;
	}
	return 1;
}

// ----------------------------------------------------------------------------
// Mapping of 2-bits for size encodings in most instruction types that allow
// all 3 sizes.
Size standard_size_table[] =
{
	Size::BYTE,
	Size::WORD,
	Size::LONG,
	Size::NONE		// Either invalid, or used for another instruction type
};

// ----------------------------------------------------------------------------
Suffix size_to_suffix(Size size)
{
	switch (size)
	{
		case Size::BYTE: return Suffix::BYTE;
		case Size::WORD: return Suffix::WORD;
		case Size::LONG: return Suffix::LONG;
		default: break;
	}
	return Suffix::NONE;
}

// ----------------------------------------------------------------------------
// Integer ALU instruction, #dds,<ea>
int Inst_integer_imm_ea(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	// op0: immediate value
	uint8_t size_bits = (header >> 6) & 3;
	Size sizes[] = { Size::BYTE, Size::WORD, Size::LONG, Size::NONE };
	Size ea_size = sizes[size_bits];
	inst.suffix = size_to_suffix(ea_size);
	if (ea_size == Size::NONE)
		return 1;

	if (read_immediate(buffer, inst.op0, ea_size))
		return 1;

	// op1: EA
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	return decode_ea(buffer, inst.op1, ea_group::DATA_ALT, mode, reg, ea_size);
}

// ----------------------------------------------------------------------------
// Single EA operand
int Inst_size_ea(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t size = (header >> 6) & 3;
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;

	Size sizes[] = { Size::BYTE, Size::WORD, Size::LONG, Size::NONE };
	Size ea_size = sizes[size];
	inst.suffix = size_to_suffix(ea_size);
	if (ea_size == Size::NONE)
		return 1;

	return decode_ea(buffer, inst.op0, ea_group::DATA_ALT, mode, reg, ea_size);
}

// ----------------------------------------------------------------------------
int Inst_lea(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	uint8_t dest_reg  = (header >> 9) & 7;

	// Target register
	inst.op1.type = OpType::A_DIRECT;
	inst.op1.a_register.reg = dest_reg;

	return decode_ea(buffer, inst.op0, ea_group::CONTROL, mode, reg, Size::LONG);
}

// ----------------------------------------------------------------------------
int Inst_movem_reg_mem(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	Size ea_size = (header >> 6) & 1 ? Size::LONG : Size::WORD;
	inst.suffix = (header >> 6) & 1 ? Suffix::LONG : Suffix::WORD;

	uint16_t reg_mask;
	if (buffer.read_word(reg_mask))
		return 1;

	// src register
	inst.op0.type = OpType::MOVEM_REG;
	inst.op0.movem_reg.reg_mask = reg_mask;
	if (decode_ea(buffer, inst.op1, ea_group::CONTROL_MOVEM1, mode, reg, ea_size))
		return 1;

	// Special case: if we are -(a0) mode, order of registers is reversed
	if (inst.op1.type == OpType::INDIRECT_PREDEC)
	{
		uint16_t rev_mask = 0;
		for (int i = 0; i < 16; ++i)
		{
			rev_mask <<= 1;
			if (reg_mask & (1 << i))
				rev_mask |= 1;
		}
		inst.op0.movem_reg.reg_mask = rev_mask;
	}
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_movem_mem_reg(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	Size ea_size = (header >> 6) & 1 ? Size::LONG : Size::WORD;
	inst.suffix = (header >> 6) & 1 ? Suffix::LONG : Suffix::WORD;
	uint16_t reg_mask;
	if (buffer.read_word(reg_mask))
		return 1;

	// dst register
	inst.op1.type = OpType::MOVEM_REG;
	inst.op1.movem_reg.reg_mask = reg_mask;
	return decode_ea(buffer, inst.op0, ea_group::CONTROL_MOVEM2, mode, reg, ea_size);
}

// ----------------------------------------------------------------------------
int Inst_move(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t sizebits = (header >> 12) & 3;
	uint8_t src_reg  = (header >> 0) & 7;
	uint8_t src_mode = (header >> 3) & 7;
	uint8_t dst_reg  = (header >> 9) & 7;
	uint8_t dst_mode = (header >> 6) & 7;

	// NOTE: this is non-standard
	Size sizes[] = { Size::NONE, Size::BYTE, Size::LONG, Size::WORD };
	Size ea_size = sizes[sizebits];
	if (ea_size == Size::NONE)
		return 1;
	inst.suffix = size_to_suffix(ea_size);

	// Src data comes first in the encoding
	int ret = decode_ea(buffer, inst.op0, ea_group::ALL, src_mode, src_reg, ea_size);
	if (ret != 0)
		return ret;
	ret = decode_ea(buffer, inst.op1, ea_group::DATA_ALT, dst_mode, dst_reg, ea_size);
	return ret;
}

// ----------------------------------------------------------------------------
int Inst_movea(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t size = (header >> 12) & 3;
	uint8_t areg = (header >> 9) & 7;
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;

	Size sizes[] = { Size::NONE, Size::NONE, Size::LONG, Size::WORD };
	Size ea_size = sizes[size];
	if (ea_size == Size::NONE)
		return 1;
	inst.suffix = size_to_suffix(ea_size);

	if (decode_ea(buffer, inst.op0, ea_group::ALL, mode, reg, ea_size))
		return 1;

	inst.op1.type = OpType::A_DIRECT;
	inst.op1.a_register.reg = areg;
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_moveq(buffer_reader& /*buffer*/, instruction& inst, uint32_t header)
{
	inst.suffix = Suffix::LONG;

	set_imm_byte(inst.op0, header & 0xff);
	uint8_t reg = (header >> 9) & 7;
	set_dreg(inst.op1, reg);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_subq(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t size = (header >> 6) & 3;
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;

	uint8_t data = (header >> 9) & 7;
	if (data == 0)
		data = 8;
	set_imm_byte(inst.op0, data);

	Size sizes[] = { Size::BYTE, Size::WORD, Size::LONG, Size::NONE };
	Size ea_size = sizes[size];
	if (ea_size == Size::NONE)
		return 1;

	inst.suffix = size_to_suffix(ea_size);
	return decode_ea(buffer, inst.op1, ea_group::ALT, mode, reg, ea_size);
}

// ----------------------------------------------------------------------------
int Inst_trap(buffer_reader& /*buffer*/, instruction& inst, uint32_t header)
{
	set_imm_byte(inst.op0, header & 0xf);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_move_from_sr(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	inst.suffix = Suffix::WORD;
	inst.op0.type = OpType::SR;

	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	return decode_ea(buffer, inst.op1, ea_group::DATA, mode, reg, Size::WORD);
}

// ----------------------------------------------------------------------------
int Inst_move_to_sr(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	inst.suffix = Suffix::WORD;
	inst.op1.type = OpType::SR;

	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	return decode_ea(buffer, inst.op0, ea_group::DATA, mode, reg, Size::WORD);
}

// ----------------------------------------------------------------------------
// bchg, bset, bclr, btst
int Inst_bchg_imm(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;

	// Read the immediate data
	uint16_t imm;
	if (buffer.read_word(imm))
		return 1;
	if (imm & 0xff00)			   // top bits must be 0
		return 1;

	set_imm_byte(inst.op0, (imm >> 0) & 0xff);
	if (decode_ea(buffer, inst.op1, ea_group::DATA_ALT, mode, reg, Size::NONE))
		return 1;

	// instruction size is LONG when using DREG.
	inst.suffix = inst.op1.type == OpType::D_DIRECT ? Suffix::LONG : Suffix::BYTE;
	return 0;
}

// ----------------------------------------------------------------------------
// bchg, bset, bclr, btst
int Inst_btst(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t bitreg = (header >> 9) & 7;
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;

	set_dreg(inst.op0, bitreg);
	return decode_ea(buffer, inst.op1, ea_group::DATA, mode, reg, Size::BYTE);
}

// ----------------------------------------------------------------------------
// bchg, bset, bclr, btst
int Inst_btst_imm(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;

	// Read the immediate data
	uint16_t imm;
	if (buffer.read_word(imm))
		return 1;
	if (imm & 0xff00)			   // top bits must be 0
		return 1;

	set_imm_byte(inst.op0, (imm >> 0) & 0xff);
	if (decode_ea(buffer, inst.op1, ea_group::DATA, mode, reg, Size::NONE))
		return 1;

	// instruction size is LONG when using DREG.
	inst.suffix = inst.op1.type == OpType::D_DIRECT ? Suffix::LONG : Suffix::BYTE;
	return 0;
}

// ----------------------------------------------------------------------------
// bchg, bset, bclr, btst
int Inst_bchg(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t bitreg = (header >> 9) & 7;
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;

	set_dreg(inst.op0, bitreg);
	return decode_ea(buffer, inst.op1, ea_group::DATA_ALT, mode, reg, Size::BYTE);
}

// ----------------------------------------------------------------------------
int Inst_alu_dreg(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t dreg = (header >> 9) & 7;
	uint8_t ea_dst = (header >> 8) & 1;
	uint8_t size   = (header >> 6) & 3;
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;

	Size sizes[] = { Size::BYTE, Size::WORD, Size::LONG, Size::NONE };
	Size ea_size = sizes[size];
	if (ea_size == Size::NONE)
		return 1;
	inst.suffix = size_to_suffix(ea_size);

	if (ea_dst)
	{
		// Src is d-reg
		inst.op0.type = OpType::D_DIRECT;
		inst.op0.d_register.reg = dreg;
		return decode_ea(buffer, inst.op1, ea_group::MEM_ALT, mode, reg, ea_size);
	}
	else
	{
		// Dest is d-reg
		inst.op1.type = OpType::D_DIRECT;
		inst.op1.d_register.reg = dreg;
		return decode_ea(buffer, inst.op0, ea_group::ALL, mode, reg, ea_size);
	}
}

// ----------------------------------------------------------------------------
int Inst_addsuba(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t areg = (header >> 9) & 7;
	uint8_t size = (header >> 8) & 1;
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;

	Size sizes[] = { Size::WORD, Size::LONG };
	Size ea_size = sizes[size];
	if (ea_size == Size::NONE)
		return 1;
	inst.suffix = size_to_suffix(ea_size);

	inst.op1.type = OpType::A_DIRECT;
	inst.op1.a_register.reg = areg;
	return decode_ea(buffer, inst.op0, ea_group::ALL, mode, reg, ea_size);
}

// ----------------------------------------------------------------------------
int Inst_shift_mem(buffer_reader& /*buffer*/, instruction& inst, uint32_t header)
{
	uint8_t shift	= (header >> 9) & 7;
	uint8_t size	 = (header >> 6) & 3;
	uint8_t countreg = (header >> 5) & 1;
	uint8_t dreg	 = (header >> 0) & 7;

	Size ea_size = standard_size_table[size];
	if (ea_size == Size::NONE)
		return 1;
	inst.suffix = size_to_suffix(ea_size);
	if (countreg == 0)
	{
		// Shift by immediate
		if (shift == 0)
			shift = 8;

		set_imm_byte(inst.op0, shift);
	}
	else
	{
		// Shift by register
		inst.op0.type = OpType::D_DIRECT;
		inst.op0.d_register.reg = shift;
	}

	inst.op1.type = OpType::D_DIRECT;
	inst.op1.d_register.reg = dreg;
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_stop(buffer_reader& buffer, instruction& inst, uint32_t /*header*/)
{
	uint16_t val;
	if (buffer.read_word(val))
		return 1;
	set_imm_word(inst.op0, val);
	return 0;
}

// ----------------------------------------------------------------------------
// Instructions without any operands
int Inst_simple(buffer_reader& /*buffer*/, instruction& /*inst*/, uint32_t /*header*/)
{
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_swap(buffer_reader& /*header*/, instruction& inst, uint32_t header)
{
	uint8_t reg = (header >> 0) & 7;
	set_dreg(inst.op0, reg);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_link_w(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t reg = (header >> 0) & 7;
	inst.suffix = Suffix::WORD;
	set_areg(inst.op0, reg);

	uint16_t disp;
	if (buffer.read_word(disp))
		return 1;

	// NO CHECK unsigned
	set_imm_word(inst.op1, disp);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_unlk(buffer_reader& /*header*/, instruction& inst, uint32_t header)
{
	uint8_t reg = (header >> 0) & 7;
	set_areg(inst.op0, reg);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_move_from_usp(buffer_reader& /*header*/, instruction& inst, uint32_t header)
{
	uint8_t reg = (header >> 0) & 7;
	inst.op0.type = OpType::USP;
	set_areg(inst.op1, reg);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_move_to_usp(buffer_reader& /*header*/, instruction& inst, uint32_t header)
{
	uint8_t reg = (header >> 0) & 7;
	set_areg(inst.op0, reg);
	inst.op1.type = OpType::USP;
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_move_to_ccr(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	inst.op1.type = OpType::CCR;
	return decode_ea(buffer, inst.op0, ea_group::DATA, mode, reg, Size::WORD);
}

// ----------------------------------------------------------------------------
int Inst_nbcd(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	inst.suffix = Suffix::BYTE;
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	return decode_ea(buffer, inst.op0, ea_group::DATA_ALT, mode, reg, Size::BYTE);
}

// ----------------------------------------------------------------------------
int Inst_pea(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	return decode_ea(buffer, inst.op0, ea_group::CONTROL, mode, reg, Size::LONG);
}

// ----------------------------------------------------------------------------
// NOTE: identical to nbcd
int Inst_tas(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	return decode_ea(buffer, inst.op0, ea_group::DATA_ALT, mode, reg, Size::BYTE);
}

// ----------------------------------------------------------------------------
// jmp/jsr
int Inst_jump(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	return decode_ea(buffer, inst.op0, ea_group::CONTROL, mode, reg, Size::NONE);
}

// ----------------------------------------------------------------------------
int Inst_asl_asr_mem(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	// "An operand in memory can be shifted one bit only, and the operand size is restricted to a word."
	inst.suffix = Suffix::WORD;
	return decode_ea(buffer, inst.op0, ea_group::MEM_ALT, mode, reg, Size::WORD);
}

// ----------------------------------------------------------------------------
int Inst_branch(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	int8_t disp8 = (int8_t)(header & 0xff);

	inst.op0.type = RELATIVE_BRANCH;
	if (disp8 == 0)
	{
		// 16-bit offset
		uint16_t disp16;
		if (buffer.read_word(disp16))
			return 1;
		inst.op0.relative_branch.disp = (int16_t)disp16;
		inst.suffix = Suffix::WORD;
	}
	else
	{
		inst.op0.relative_branch.disp = disp8;
		inst.suffix = Suffix::SHORT;
	}
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_ext(buffer_reader& /*header*/, instruction& inst, uint32_t header)
{
	// NOTE: this needs to be changed if handling ext byte->long
	uint8_t mode = (header >> 6) & 1;
	inst.suffix = (mode == 0) ? Suffix::WORD : Suffix::LONG;
	uint8_t reg = (header >> 0) & 7;
	set_dreg(inst.op0, reg);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_movep_mem_reg(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 6) & 1;
	inst.suffix = (mode == 0) ? Suffix::WORD : Suffix::LONG;

	uint8_t dreg = (header >> 9) & 7;
	uint8_t areg = (header >> 0) & 7;
	uint16_t val16;
	if (buffer.read_word(val16))
		return 1;

	inst.op0.type = OpType::INDIRECT_DISP;
	inst.op0.indirect_disp.reg = areg;
	inst.op0.indirect_disp.disp = (int16_t)val16;
	set_dreg(inst.op1, dreg);

	return 0;
}

// ----------------------------------------------------------------------------
int Inst_movep_reg_mem(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 6) & 1;
	inst.suffix = (mode == 0) ? Suffix::WORD : Suffix::LONG;

	uint8_t dreg = (header >> 9) & 7;
	uint8_t areg = (header >> 0) & 7;
	uint16_t val16;
	if (buffer.read_word(val16))
		return 1;

	set_dreg(inst.op0, dreg);
	inst.op1.type = OpType::INDIRECT_DISP;
	inst.op1.indirect_disp.reg = areg;
	inst.op1.indirect_disp.disp = (int16_t)val16;
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_dbcc(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t dreg  = (header >> 0) & 7;
	uint16_t disp16;
	if (buffer.read_word(disp16))
		return 1;

	set_dreg(inst.op0, dreg);
	inst.op1.type = RELATIVE_BRANCH;
	inst.op1.relative_branch.disp = (int16_t)disp16;
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_scc(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	return decode_ea(buffer, inst.op0, ea_group::DATA_ALT, mode, reg, Size::BYTE);
}

// ----------------------------------------------------------------------------
int Inst_sbcd_reg(buffer_reader& /*header*/, instruction& inst, uint32_t header)
{
	uint8_t regx = (header >> 0) & 7;
	uint8_t regy = (header >> 9) & 7;
	set_dreg(inst.op0, regx);
	set_dreg(inst.op1, regy);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_sbcd_predec(buffer_reader& /*header*/, instruction& inst, uint32_t header)
{
	uint8_t regx = (header >> 0) & 7;
	uint8_t regy = (header >> 9) & 7;
	set_predec(inst.op0, regx);
	set_predec(inst.op1, regy);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_subx_reg(buffer_reader& /*header*/, instruction& inst, uint32_t header)
{
	uint8_t size = (header >> 6) & 3;
	Size ea_size = standard_size_table[size];
	if (ea_size == Size::NONE)
		return 1;
	inst.suffix = size_to_suffix(ea_size);
	uint8_t regx = (header >> 0) & 7;
	uint8_t regy = (header >> 9) & 7;
	set_dreg(inst.op0, regx);
	set_dreg(inst.op1, regy);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_subx_predec(buffer_reader& /*header*/, instruction& inst, uint32_t header)
{
	uint8_t size = (header >> 6) & 3;
	Size ea_size = standard_size_table[size];
	if (ea_size == Size::NONE)
		return 1;
	inst.suffix = size_to_suffix(ea_size);

	uint8_t regx = (header >> 0) & 7;
	uint8_t regy = (header >> 9) & 7;
	set_predec(inst.op0, regx);
	set_predec(inst.op1, regy);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_cmpm(buffer_reader& /*header*/, instruction& inst, uint32_t header)
{
	uint8_t size = (header >> 6) & 3;
	Size ea_size = standard_size_table[size];
	if (ea_size == Size::NONE)
		return 1;
	inst.suffix = size_to_suffix(ea_size);

	uint8_t regx = (header >> 0) & 7;
	uint8_t regy = (header >> 9) & 7;
	set_postinc(inst.op0, regx);
	set_postinc(inst.op1, regy);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_cmpa(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	uint8_t size = (header >> 8) & 1;
	uint8_t areg = (header >> 9) & 7;

	Size ea_size = size ? Size::LONG : Size::WORD;
	if (ea_size == Size::NONE)
		return 1;
	inst.suffix = size_to_suffix(ea_size);

	if (decode_ea(buffer, inst.op0, ea_group::ALL, mode, reg, ea_size))
		return 1;
	set_areg(inst.op1, areg);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_cmp(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	uint8_t size = (header >> 6) & 3;
	uint8_t dreg = (header >> 9) & 7;
	Size ea_size = standard_size_table[size];
	if (ea_size == Size::NONE)
		return 1;
	inst.suffix = size_to_suffix(ea_size);

	if (decode_ea(buffer, inst.op0, ea_group::ALL, mode, reg, ea_size))
		return 1;
	set_dreg(inst.op1, dreg);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_eor(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	uint8_t size = (header >> 6) & 3;
	uint8_t dreg = (header >> 9) & 7;
	Size ea_size = standard_size_table[size];
	if (ea_size == Size::NONE)
		return 1;
	inst.suffix = size_to_suffix(ea_size);

	// src is d-reg
	set_dreg(inst.op0, dreg);

	// dest is EA
	if (decode_ea(buffer, inst.op1, ea_group::DATA_ALT, mode, reg, ea_size))
		return 1;
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_muldiv(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	uint8_t dreg = (header >> 9) & 7;
	Size ea_size = Size::WORD;
	if (ea_size == Size::NONE)
		return 1;
	inst.suffix = Suffix::WORD;

	// src is EA
	if (decode_ea(buffer, inst.op0, ea_group::DATA, mode, reg, ea_size))
		return 1;

	// dst is d-reg
	set_dreg(inst.op1, dreg);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_chk(buffer_reader& buffer, instruction& inst, uint32_t header)
{
	uint8_t dreg = (header >> 9) & 7;
	uint8_t size = (header >> 7) & 1;
	uint8_t mode = (header >> 3) & 7;
	uint8_t reg  = (header >> 0) & 7;
	Size ea_size = size ? Size::WORD : Size::LONG;
	// Long size only allowed on 68020+
	if (ea_size == Size::LONG)
		return 1;

	inst.suffix = size_to_suffix(ea_size);

	set_dreg(inst.op1, dreg);
	return decode_ea(buffer, inst.op0, ea_group::DATA, mode, reg, ea_size);
}

// ----------------------------------------------------------------------------
int Inst_exg_dd(buffer_reader& /*header*/, instruction& inst, uint32_t header)
{
	uint8_t regx = (header >> 0) & 7;
	uint8_t regy = (header >> 9) & 7;
	set_dreg(inst.op0, regx);
	set_dreg(inst.op1, regy);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_exg_aa(buffer_reader& /*header*/, instruction& inst, uint32_t header)
{
	uint8_t regx = (header >> 0) & 7;
	uint8_t regy = (header >> 9) & 7;
	set_areg(inst.op0, regx);
	set_areg(inst.op1, regy);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_exg_da(buffer_reader& /*header*/, instruction& inst, uint32_t header)
{
	uint8_t regx = (header >> 0) & 7;
	uint8_t regy = (header >> 9) & 7;
	set_dreg(inst.op0, regx);
	set_areg(inst.op1, regy);
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_imm_ccr(buffer_reader& buffer, instruction& inst, uint32_t /*header*/)
{
	uint16_t val16;
	if (buffer.read_word(val16))
		return 1;
	if ((val16 & 0xff00) != 0)
		return 1;

	set_imm_byte(inst.op0, val16 & 0xff);
	inst.op1.type = OpType::CCR;
	return 0;
}

// ----------------------------------------------------------------------------
int Inst_imm_sr(buffer_reader& buffer, instruction& inst, uint32_t /*header*/)
{
	uint16_t val16;
	if (buffer.read_word(val16))
		return 1;

	set_imm_word(inst.op0, val16);
	inst.op1.type = OpType::SR;
	return 0;
}

// ----------------------------------------------------------------------------
typedef int (*pfnDecoderFunc)(buffer_reader& buffer, instruction& inst, uint32_t header);

// ===========================================================
//   MATCHER TABLE
// ===========================================================
//
//	This is the main dispatch table. It matches partial bitstreams to
//	call through to a handler class.
//
//	Entries go in the order Most specific -> Least Specific
//	i.e. they should go in terms of decreasing "CT" i.e. bitcount

// TODO switch tag to an instruction type
struct matcher_entry
{
	uint32_t		mask0;
	uint32_t		val0;
	Opcode			opcode;
	pfnDecoderFunc	func;
};

#define MATCH_ENTRY1_IMPL(shift, bitcount, val, tag, func)   \
	{ (((1U<<(bitcount))-1U)<<(shift)), (val<<(shift)), Opcode::tag, func }

#define MATCH_ENTRY2_IMPL(shift, bitcount, val, shift2, bitcount2, val2, tag, func)   \
	{ (((1U<<(bitcount))-1U)<<(shift)) | (((1U<<(bitcount2))-1U)<<(shift2)), \
		(val<<(shift)) | (val2<<(shift2)), \
	   Opcode::tag, func }

#define MATCH_ENTRY3_IMPL(shift, bitcount, val, shift2, bitcount2, val2, shift3, bitcount3, val3, tag, func)   \
	{ (((1U<<(bitcount))-1U)<<(shift)) | (((1U<<(bitcount2))-1U)<<(shift2))  | (((1U<<(bitcount3))-1U)<<(shift3)), \
		(val<<(shift)) | (val2<<(shift2)) | (val3<<(shift3)), \
	   Opcode::tag, func }

#define MATCH_END		{ 0, 0, Opcode::COUNT, NULL}

const matcher_entry g_matcher_table_0000[] =
{
	//		          SH CT							Tag				  Decoder
	MATCH_ENTRY1_IMPL(0,16,0b0000101001111100,		EORI,		Inst_imm_sr ), // supervisor
	MATCH_ENTRY1_IMPL(0,16,0b0000001000111100,		ANDI,		Inst_imm_ccr ),
	MATCH_ENTRY1_IMPL(0,16,0b0000101000111100,		EORI,		Inst_imm_ccr ),
	MATCH_ENTRY1_IMPL(0,16,0b0000000000111100,		ORI,		Inst_imm_ccr ),
	MATCH_ENTRY1_IMPL(0,16,0b0000000001111100,		ORI,		Inst_imm_sr ), // supervisor
	MATCH_ENTRY1_IMPL(0,16,0b0000001001111100,		ANDI,		Inst_imm_sr ), // supervisor

	MATCH_ENTRY2_IMPL(12,4,0b0000, 3,6,0b100001,	MOVEP,		Inst_movep_mem_reg ),
	MATCH_ENTRY2_IMPL(12,4,0b0000, 3,6,0b101001,	MOVEP,		Inst_movep_mem_reg ),
	MATCH_ENTRY2_IMPL(12,4,0b0000, 3,6,0b110001,	MOVEP,		Inst_movep_reg_mem ),
	MATCH_ENTRY2_IMPL(12,4,0b0000, 3,6,0b111001,	MOVEP,		Inst_movep_reg_mem ),
	MATCH_ENTRY1_IMPL(6,10,0b0000100001,			BCHG,		Inst_bchg_imm ),
	MATCH_ENTRY1_IMPL(6,10,0b0000100010,			BCLR,		Inst_bchg_imm ),
	MATCH_ENTRY1_IMPL(6,10,0b0000100011,			BSET,		Inst_bchg_imm ),
	MATCH_ENTRY1_IMPL(6,10,0b0000100000,			BTST,		Inst_btst_imm ),

	MATCH_ENTRY1_IMPL(8,8,0b00000000,				ORI,		Inst_integer_imm_ea ),
	MATCH_ENTRY1_IMPL(8,8,0b00000010,				ANDI,		Inst_integer_imm_ea ),
	MATCH_ENTRY1_IMPL(8,8,0b00000100,				SUBI,		Inst_integer_imm_ea ),
	MATCH_ENTRY1_IMPL(8,8,0b00000110,				ADDI,		Inst_integer_imm_ea ),
	MATCH_ENTRY1_IMPL(8,8,0b00001010,				EORI,		Inst_integer_imm_ea ),
	MATCH_ENTRY1_IMPL(8,8,0b00001100,				CMPI,		Inst_integer_imm_ea ),

	MATCH_ENTRY2_IMPL(12,4,0b0000, 6,3,0b101,		BCHG,		Inst_bchg ),
	MATCH_ENTRY2_IMPL(12,4,0b0000, 6,3,0b110,		BCLR,		Inst_bchg ),
	MATCH_ENTRY2_IMPL(12,4,0b0000, 6,3,0b111,		BSET,		Inst_bchg ),
	MATCH_ENTRY2_IMPL(12,4,0b0000, 6,3,0b100,		BTST,		Inst_btst ),

	MATCH_ENTRY1_IMPL(12,4,0b0000,					MOVE,		Inst_move ),
	MATCH_END
};

const matcher_entry g_matcher_table_0001[] =
{
	MATCH_ENTRY1_IMPL(12,4,0b0001,					MOVE,		Inst_move ),
	MATCH_END
};

const matcher_entry g_matcher_table_0010[] =
{
	MATCH_ENTRY2_IMPL(12,4,0b0010, 6,3,0b001,		MOVEA,		Inst_movea ),
	MATCH_ENTRY1_IMPL(12,4,0b0010,					MOVE,		Inst_move ),
	MATCH_END
};

const matcher_entry g_matcher_table_0011[] =
{
	MATCH_ENTRY2_IMPL(12,4,0b0011, 6,3,0b001,		MOVEA,		Inst_movea ),
	MATCH_ENTRY1_IMPL(12,4,0b0011,					MOVE,		Inst_move ),
	MATCH_END
};

const matcher_entry g_matcher_table_0100[] =
{
	MATCH_ENTRY1_IMPL(0,16,0b0100101011111100,		ILLEGAL,	Inst_simple ),
	MATCH_ENTRY1_IMPL(0,16,0b0100111001110000,		RESET,		Inst_simple ), // supervisor
	MATCH_ENTRY1_IMPL(0,16,0b0100111001110001,		NOP,		Inst_simple ),
	MATCH_ENTRY1_IMPL(0,16,0b0100111001110011,		RTE,		Inst_simple ), // supervisor
	MATCH_ENTRY1_IMPL(0,16,0b0100111001110101,		RTS,		Inst_simple ),
	MATCH_ENTRY1_IMPL(0,16,0b0100111001110110,		TRAPV,		Inst_simple ),
	MATCH_ENTRY1_IMPL(0,16,0b0100111001110111,		RTR,		Inst_simple ),
	MATCH_ENTRY1_IMPL(0,16,0b0100111001110010,		STOP,		Inst_stop ),

	MATCH_ENTRY1_IMPL(3,13,0b0100100001000,			SWAP,		Inst_swap ),
	MATCH_ENTRY1_IMPL(3,13,0b0100111001010,			LINK,		Inst_link_w ),
	//([ ( 3,13, 0b0100100000001)			  ,		"LINK.L",	Inst_link_l ),  # not on 68000
	MATCH_ENTRY1_IMPL(3,13,0b0100111001011,			UNLK,		Inst_unlk ),
	MATCH_ENTRY1_IMPL(3,13,0b0100111001100,			MOVE,		Inst_move_to_usp ),
	MATCH_ENTRY1_IMPL(3,13,0b0100111001101,			MOVE,		Inst_move_from_usp ),
	MATCH_ENTRY1_IMPL(3,13,0b0100100010000,			EXT,		Inst_ext ),
	MATCH_ENTRY1_IMPL(3,13,0b0100100011000,			EXT,		Inst_ext ),
	//([ ( 3,13, 0b0100100011000)			  ,		"EXTB.L",	Inst_ext ),	 # not on 68000

	MATCH_ENTRY1_IMPL(4,12,0b010011100100,			TRAP,		Inst_trap ),

	MATCH_ENTRY1_IMPL(6,10,0b0100000011,			MOVE,		Inst_move_from_sr ),   // supervisor
	MATCH_ENTRY1_IMPL(6,10,0b0100011011,			MOVE,		Inst_move_to_sr ),   // supervisor
	//([ ( 6,10, 0b0100001011)				 ,		"MOVE FROM Ccr",	 Inst ),		  # not on 68000
	MATCH_ENTRY1_IMPL(6,10,0b0100010011,			MOVE,		Inst_move_to_ccr ),
	MATCH_ENTRY1_IMPL(6,10,0b0100100000,			NBCD,		Inst_nbcd ),
	MATCH_ENTRY1_IMPL(6,10,0b0100100001,			PEA,		Inst_pea ),
	MATCH_ENTRY1_IMPL(6,10,0b0100101011,			TAS,		Inst_tas ),
	MATCH_ENTRY1_IMPL(6,10,0b0100111010,			JSR,		Inst_jump ),
	MATCH_ENTRY1_IMPL(6,10,0b0100111011,			JMP,		Inst_jump ),

	MATCH_ENTRY1_IMPL(7,9,0b010010001,				MOVEM,		Inst_movem_reg_mem ), // Register to memory.
	MATCH_ENTRY1_IMPL(7,9,0b010011001,				MOVEM,		Inst_movem_mem_reg ), // Memory to register.

	MATCH_ENTRY1_IMPL(8,8,0b01000000,				NEGX,		Inst_size_ea ),
	MATCH_ENTRY1_IMPL(8,8,0b01000010,				CLR,		Inst_size_ea ),
	MATCH_ENTRY1_IMPL(8,8,0b01000100,				NEG,		Inst_size_ea ),
	MATCH_ENTRY1_IMPL(8,8,0b01000110,				NOT,		Inst_size_ea ),
	MATCH_ENTRY1_IMPL(8,8,0b01001010,				TST,		Inst_size_ea ),

	MATCH_ENTRY2_IMPL(12,4,0b0100, 6,3,0b111,		LEA,		Inst_lea ),

	MATCH_ENTRY2_IMPL(12,4,0b0100, 6,3,0b110,		CHK,		Inst_chk ),
	MATCH_ENTRY2_IMPL(12,4,0b0100, 6,3,0b100,		CHK,		Inst_chk ),	// not 68000

	MATCH_END
};

const matcher_entry g_matcher_table_0101[] =
{
	//Table 3-19. Conditional TESTS
	// These sneakily take the "001" in the bottom 3 BITS TO OVErride the EA parts of Scc
	MATCH_ENTRY1_IMPL(3,13,0b0101000111001,			DBF,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101001011001,			DBHI,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101001111001,			DBLS,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101010011001,			DBCC,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101010111001,			DBCS,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101011011001,			DBNE,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101011111001,			DBEQ,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101100011001,			DBVC,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101100111001,			DBVS,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101101011001,			DBPL,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101101111001,			DBMI,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101110011001,			DBGE,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101110111001,			DBLT,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101111011001,			DBGT,		Inst_dbcc ),
	MATCH_ENTRY1_IMPL(3,13,0b0101111111001,			DBLE,		Inst_dbcc ),

	MATCH_ENTRY1_IMPL(6,10,0b0101000011,			ST,			Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101000111,			SF,			Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101001011,			SHI,		Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101001111,			SLS,		Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101010011,			SCC,		Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101010111,			SCS,		Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101011011,			SNE,		Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101011111,			SEQ,		Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101100011,			SVC,		Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101100111,			SVS,		Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101101011,			SPL,		Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101101111,			SMI,		Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101110011,			SGE,		Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101110111,			SLT,		Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101111011,			SGT,		Inst_scc ),
	MATCH_ENTRY1_IMPL(6,10,0b0101111111,			SLE,		Inst_scc ),

	MATCH_ENTRY2_IMPL(12,4,0b0101, 8,1,0b1,			SUBQ,		Inst_subq ),
	MATCH_ENTRY2_IMPL(12,4,0b0101, 8,1,0b0,			ADDQ,		Inst_subq ),
	MATCH_END
};

const matcher_entry g_matcher_table_0110[] =
{
	MATCH_ENTRY1_IMPL(8,8,0b01100000,				BRA,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01100001,				BSR,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01100010,				BHI,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01100011,				BLS,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01100100,				BCC,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01100101,				BCS,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01100110,				BNE,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01100111,				BEQ,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01101000,				BVC,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01101001,				BVS,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01101010,				BPL,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01101011,				BMI,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01101100,				BGE,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01101101,				BLT,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01101110,				BGT,		Inst_branch ),
	MATCH_ENTRY1_IMPL(8,8,0b01101111,				BLE,		Inst_branch ),

	MATCH_END
};

const matcher_entry g_matcher_table_0111[] =
{
	MATCH_ENTRY2_IMPL(12,4,0b0111, 8,1,0b0,			MOVEQ,		Inst_moveq ),

	MATCH_END
};

const matcher_entry g_matcher_table_1000[] =
{
	MATCH_ENTRY2_IMPL(12,4,0b1000, 3,6,0b100000,	SBCD,		Inst_sbcd_reg ),
	MATCH_ENTRY2_IMPL(12,4,0b1000, 3,6,0b100001,	SBCD,		Inst_sbcd_predec ),
	MATCH_ENTRY2_IMPL(12,4,0b1000, 6,3,0b011,		DIVU,		Inst_muldiv ),
	MATCH_ENTRY2_IMPL(12,4,0b1000, 6,3,0b111,		DIVS,		Inst_muldiv ),
	MATCH_ENTRY1_IMPL(12,4,0b1000,					OR,			Inst_alu_dreg ),
	MATCH_END
};

const matcher_entry g_matcher_table_1001[] =
{
	MATCH_ENTRY2_IMPL(12,4,0b1001, 6,2,0b11,		SUBA,		Inst_addsuba ),
	MATCH_ENTRY3_IMPL(12,4,0b1001, 8,1,1, 3,3,0,	SUBX,		Inst_subx_reg ),
	MATCH_ENTRY3_IMPL(12,4,0b1001, 8,1,1, 3,3,1,	SUBX,		Inst_subx_predec ),
	MATCH_ENTRY1_IMPL(12,4,0b1001,					SUB,		Inst_alu_dreg ),
	MATCH_END
};

const matcher_entry g_matcher_table_1010[] =
{
	MATCH_END
};

const matcher_entry g_matcher_table_1011[] =
{
	MATCH_ENTRY2_IMPL(12,4,0b1011, 6,2,3,			CMPA,		Inst_cmpa ),
	MATCH_ENTRY3_IMPL(12,4,0b1011, 8,1,1, 3,3,1,	CMPM,		Inst_cmpm ),
	// Nasty case where eor and cmp mirror one another
	MATCH_ENTRY2_IMPL(12,4,0b1011, 6,3,0b100,		EOR,		Inst_eor ),
	MATCH_ENTRY2_IMPL(12,4,0b1011, 6,3,0b101,		EOR,		Inst_eor ),
	MATCH_ENTRY2_IMPL(12,4,0b1011, 6,3,0b110,		EOR,		Inst_eor ),
	// Fallback GENERICS
	MATCH_ENTRY1_IMPL(12,4,0b1011,					CMP,		Inst_cmp ),
	MATCH_END
};

const matcher_entry g_matcher_table_1100[] =
{
	MATCH_ENTRY2_IMPL(12,4,0b1100, 3,6,0b100000,	ABCD,		Inst_sbcd_reg ),
	MATCH_ENTRY2_IMPL(12,4,0b1100, 3,6,0b100001,	ABCD,		Inst_sbcd_predec ),
	MATCH_ENTRY2_IMPL(12,4,0b1100,    6,3,0b011,	MULU,		Inst_muldiv ),
	MATCH_ENTRY2_IMPL(12,4,0b1100,    6,3,0b111,	MULS,		Inst_muldiv ),
	MATCH_ENTRY2_IMPL(12,4,0b1100, 3,6,0b101000,	EXG,		Inst_exg_dd ),
	MATCH_ENTRY2_IMPL(12,4,0b1100, 3,6,0b101001,	EXG,		Inst_exg_aa ),
	MATCH_ENTRY2_IMPL(12,4,0b1100, 3,6,0b110001,	EXG,		Inst_exg_da ),
	MATCH_ENTRY1_IMPL(12,4,0b1100,					AND,		Inst_alu_dreg ),
	MATCH_END
};

const matcher_entry g_matcher_table_1101[] =
{
	MATCH_ENTRY2_IMPL(12,4,0b1101, 6,2,0b11,		ADDA,		Inst_addsuba ),	// more specific than ADDX
	MATCH_ENTRY3_IMPL(12,4,0b1101, 8,1,1, 3,3,0,	ADDX,		Inst_subx_reg ),
	MATCH_ENTRY3_IMPL(12,4,0b1101, 8,1,1, 3,3,1,	ADDX,		Inst_subx_predec ),
	MATCH_ENTRY1_IMPL(12,4,0b1101,					ADD,		Inst_alu_dreg ),
	MATCH_END
};

const matcher_entry g_matcher_table_1110[] =
{
	MATCH_ENTRY1_IMPL(6,10,0b1110000011,			ASR,		Inst_asl_asr_mem ),
	MATCH_ENTRY1_IMPL(6,10,0b1110000111,			ASL,		Inst_asl_asr_mem ),

	MATCH_ENTRY1_IMPL(6,10,0b1110011011,			ROR,		Inst_asl_asr_mem ),
	MATCH_ENTRY1_IMPL(6,10,0b1110011111,			ROL,		Inst_asl_asr_mem ),

	MATCH_ENTRY1_IMPL(6,10,0b1110001011,			LSR,		Inst_asl_asr_mem ),
	MATCH_ENTRY1_IMPL(6,10,0b1110001111,			LSL,		Inst_asl_asr_mem ),

	MATCH_ENTRY3_IMPL(12,4,0b1110, 3,2,0, 8,1,1,	ASL,		Inst_shift_mem ),
	MATCH_ENTRY3_IMPL(12,4,0b1110, 3,2,0, 8,1,0,	ASR,		Inst_shift_mem ),
	MATCH_ENTRY3_IMPL(12,4,0b1110, 3,2,1, 8,1,1,	LSL,		Inst_shift_mem ),
	MATCH_ENTRY3_IMPL(12,4,0b1110, 3,2,1, 8,1,0,	LSR,		Inst_shift_mem ),
	MATCH_ENTRY3_IMPL(12,4,0b1110, 3,2,3, 8,1,1,	ROL,		Inst_shift_mem ),
	MATCH_ENTRY3_IMPL(12,4,0b1110, 3,2,3, 8,1,0,	ROR,		Inst_shift_mem ),

	MATCH_ENTRY3_IMPL(12,4,0b1110, 3,2,2, 8,1,1,	ROXL,		Inst_shift_mem ),
	MATCH_ENTRY3_IMPL(12,4,0b1110, 3,2,2, 8,1,0,	ROXR,		Inst_shift_mem ),
	MATCH_END
};

const matcher_entry g_matcher_table_1111[] =
{
	MATCH_END
};

const matcher_entry* g_matcher_tables[16] =
{
	g_matcher_table_0000,
	g_matcher_table_0001,
	g_matcher_table_0010,
	g_matcher_table_0011,
	g_matcher_table_0100,
	g_matcher_table_0101,
	g_matcher_table_0110,
	g_matcher_table_0111,
	g_matcher_table_1000,
	g_matcher_table_1001,
	g_matcher_table_1010,
	g_matcher_table_1011,
	g_matcher_table_1100,
	g_matcher_table_1101,
	g_matcher_table_1110,
	g_matcher_table_1111,
};

// ----------------------------------------------------------------------------
// decode a single instruction if possible
int decode(buffer_reader& buffer, instruction& inst)
{
	inst.byte_count = 2;	// assume error
	inst.opcode = Opcode::NONE;
	inst.suffix = Suffix::NONE;

	// Check remaining size
	uint16_t header0 = 0;
	uint32_t start_pos = buffer.get_pos();

	if (buffer.get_remain() < 2)
		return 1;

	buffer.read_word(header0);
	inst.header = header0;

	// Make a temp copy of the reader to pass to the decoder, after the first word
	buffer_reader reader_tmp = buffer;
	uint16_t table = (header0 >> 12) & 0xf;
	for (const matcher_entry* pEntry = g_matcher_tables[table];
		pEntry->mask0 != 0;
		++pEntry)
	{
		assert(((pEntry->val0 >> 12) & 0xf) == table);
		// Choose 16 or 32 bits for the check
		uint32_t header = header0;
		if ((header & pEntry->mask0) != pEntry->val0)
			continue;

		// Do specialised decoding
		inst.opcode = pEntry->opcode;
		int res = 0;
		if (pEntry->func)
			res = pEntry->func(reader_tmp, inst, header);
		
		if (res)
		{
			// Handle decode func being partway through and failing
			inst.opcode = Opcode::NONE;
			inst.op0.type = OpType::INVALID;
			inst.op1.type = OpType::INVALID;
			return res;	// failed to decode
		}

		inst.byte_count = reader_tmp.get_pos() - start_pos;
		return res;
	}

	// no match found
	return 1;
}
