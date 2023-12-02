#ifndef FONDA_LIB_ELF_STRUCT_H
#define FONDA_LIB_ELF_STRUCT_H
#include <stdint.h>

/* Reference: https://refspecs.linuxfoundation.org/elf/gabi4+/contents.html */

/* e_version */
#define EV_NONE    0
#define EV_CURRENT 1

/* EI_CLASS */
#define ELFCLASSNONE 0
#define ELFCLASS32   1
#define ELFCLASS64   2

/* EI_DATA */
#define ELFDATANONE 0
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

/* sh_type */
#define SHT_NULL        0           /* Section header table entry unused */
#define SHT_PROGBITS    1           /* Program specific (private) data */
#define SHT_SYMTAB      2           /* Link editing symbol table */
#define SHT_STRTAB      3           /* A string table */
#define SHT_RELA        4           /* Relocation entries with addends */
#define SHT_HASH        5           /* A symbol hash table */
#define SHT_DYNAMIC     6           /* Information for dynamic linking */
#define SHT_NOTE        7           /* Information that marks file */
#define SHT_NOBITS      8           /* Section occupies no space in file */
#define SHT_REL         9           /* Relocation entries, no addends */
#define SHT_SHLIB       10          /* Reserved, unspecified semantics */
#define SHT_DYNSYM      11          /* Dynamic linking symbol table */
#define SHT_LOPROC      0x70000000  /* Processor-specific semantics, lo */
#define SHT_HIPROC      0x7FFFFFFF  /* Processor-specific semantics, hi */
#define SHT_LOUSER      0x80000000  /* Application-specific semantics */
#define SHT_HIUSER      0x8FFFFFFF  /* Application-specific semantics */

/* sh_flags */
#define SHF_WRITE     (1 << 0)      /* Writable data during execution */
#define SHF_ALLOC     (1 << 1)      /* Occupies memory during execution */
#define SHF_EXECINSTR (1 << 2)      /* Executable machine instructions */
#define SHF_MASKPROC  0xF0000000    /* Processor-specific semantics */

/* ST_BIND */
#define STB_LOCAL  0                /* Symbol not visible outside obj */
#define STB_GLOBAL 1                /* Symbol visible outside obj */
#define STB_WEAK   2                /* Like globals, lower precedence */
#define STB_LOPROC 13               /* Application-specific semantics */
#define STB_HIPROC 15               /* Application-specific semantics */

/* ST_TYPE */
#define STT_NOTYPE  0               /* Symbol type is unspecified */
#define STT_OBJECT  1               /* Symbol is a data object */
#define STT_FUNC    2               /* Symbol is a code object */
#define STT_SECTION 3               /* Symbol associated with a section */
#define STT_FILE    4               /* Symbol gives a file name */
#define STT_LOPROC  13              /* Application-specific semantics */
#define STT_HIPROC  15              /* Application-specific semantics */

#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2

struct Elf_Ident {
  uint8_t	ei_magic[4];           /* ELF "magic number"  0x7f / 'E' / 'L' / 'F' */
  uint8_t	ei_class;              /* File class  1==32 bits 2==64 bits */
  uint8_t	ei_data;               /* Data encoding 1==LSB first, 2==MSG first */
  uint8_t	ei_version;            /* File version */
  uint8_t	ei_osabi;              /* Operating system/ABI identification */
  uint8_t	ei_abiversion;         /* ABI version */
  uint8_t	ei_pad[7];             /* Start of padding bytes */
};

/* These are after "ElfIdent" */
struct Elf32_Ehdr {
  uint8_t	e_type[2];          /* Identifies object file type */
  uint8_t	e_machine[2];       /* Specifies required architecture */
  uint8_t	e_version[4];       /* Identifies object file version */
  uint8_t	e_entry[4];         /* Entry point virtual address */
  uint8_t	e_phoff[4];         /* Program header table file offset */
  uint8_t	e_shoff[4];         /* Section header table file offset */
  uint8_t	e_flags[4];         /* Processor-specific flags */
  uint8_t	e_ehsize[2];        /* ELF header size in bytes */
  uint8_t	e_phentsize[2];     /* Program header table entry size */
  uint8_t	e_phnum[2];         /* Program header table entry count */
  uint8_t	e_shentsize[2];     /* Section header table entry size */
  uint8_t	e_shnum[2];         /* Section header table entry count */
  uint8_t	e_shstrndx[2];      /* Section header string table index */
};

struct Elf64_Ehdr {
  uint8_t	e_type[2];          /* Identifies object file type */
  uint8_t	e_machine[2];       /* Specifies required architecture */
  uint8_t	e_version[4];       /* Identifies object file version */
  uint8_t	e_entry[8];         /* Entry point virtual address */
  uint8_t	e_phoff[8];         /* Program header table file offset */
  uint8_t	e_shoff[8];         /* Section header table file offset */
  uint8_t	e_flags[4];         /* Processor-specific flags */
  uint8_t	e_ehsize[2];        /* ELF header size in bytes */
  uint8_t	e_phentsize[2];     /* Program header table entry size */
  uint8_t	e_phnum[2];         /* Program header table entry count */
  uint8_t	e_shentsize[2];     /* Section header table entry size */
  uint8_t	e_shnum[2];         /* Section header table entry count */
  uint8_t	e_shstrndx[2];      /* Section header string table index */
};

// Wrappers for LSB/MSB conversion
struct Elf16 {
	uint8_t	data[2];
};
struct Elf32 {
	uint8_t	data[4];
};
struct Elf64 {
	uint8_t	data[8];
};

/* st_info */
struct Elf32_Sym {
  uint8_t	st_name[4];         /* Symbol name, index in string tbl */
  uint8_t	st_value[4];        /* Value of the symbol */
  uint8_t	st_size[4];         /* Associated symbol size */
  uint8_t	st_info[1];         /* Type and binding attributes */
  uint8_t	st_other[1];        /* No defined meaning, 0 */
  uint8_t	st_shndx[2];        /* Associated section index */
};

struct Elf64_Sym {
  uint8_t	st_name[4];         /* Symbol name, index in string tbl */
  uint8_t	st_info[1];         /* Type and binding attributes */
  uint8_t	st_other[1];        /* No defined meaning, 0 */
  uint8_t	st_shndx[2];        /* Associated section index */
  uint8_t	st_value[8];        /* Value of the symbol */
  uint8_t	st_size[8];         /* Associated symbol size */
};


#endif // FONDA_LIB_ELF_STRUCT_H

