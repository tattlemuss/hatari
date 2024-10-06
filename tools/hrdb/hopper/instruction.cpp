#include "instruction.h"

namespace hop68
{

const char* g_opcode_names[Opcode::COUNT] =
{
	"none",
	"abcd",
	"add",
	"adda",
	"addi",
	"addq",
	"addx",
	"and",
	"andi",
	"asl",
	"asr",
	"bcc",
	"bchg",
	"bclr",
	"bcs",
	"beq",
	"bfchg",
	"bfclr",
	"bfexts",
	"bfextu",
	"bfffo",
	"bfins",
	"bfset",
	"bftst",
	"bge",
	"bgt",
	"bhi",
	"bkpt",
	"ble",
	"bls",
	"blt",
	"bmi",
	"bne",
	"bpl",
	"bra",
	"bset",
	"bsr",
	"btst",
	"bvc",
	"bvs",
	"callm",
	"cas",
	"cas2",
	"chk",
	"chk2",
	"clr",
	"cmp",
	"cmp2",
	"cmpi",
	"cmpa",
	"cmpm",
	"dbcc",
	"dbcs",
	"dbeq",
	"dbf",
	"dbge",
	"dbgt",
	"dbhi",
	"dble",
	"dbls",
	"dblt",
	"dbmi",
	"dbne",
	"dbpl",
	"dbvc",
	"dbvs",
	"divs",
	"divsl",
	"divu",
	"divul",
	"eor",
	"eori",
	"exg",
	"ext",
	"extb",
	"illegal",
	"jmp",
	"jsr",
	"lea",
	"link",
	"lsl",
	"lsr",
	"move",
	"movea",
	"movec",
	"movem",
	"movep",
	"moveq",
	"moves",
	"muls",
	"mulu",
	"nbcd",
	"neg",
	"negx",
	"nop",
	"not",
	"or",
	"ori",
	"pack",
	"pea",
	"reset",
	"rol",
	"ror",
	"roxl",
	"roxr",
	"rtd",
	"rte",
	"rtm",
	"rtr",
	"rts",
	"sbcd",
	"scc",
	"scs",
	"seq",
	"sf",
	"sge",
	"sgt",
	"shi",
	"sle",
	"sls",
	"slt",
	"smi",
	"sne",
	"spl",
	"st",
	"stop",
	"sub",
	"suba",
	"subi",
	"subq",
	"subx",
	"svc",
	"svs",
	"swap",
	"tas",
	"trap",
	"trapcc",
	"trapcs",
	"trapeq",
	"trapf",
	"trapge",
	"trapgt",
	"traphi",
	"traple",
	"trapls",
	"traplt",
	"trapmi",
	"trapne",
	"trappl",
	"trapt",
	"trapv",
	"trapvc",
	"trapvs",
	"tst",
	"unlk",
	"unpk"
};

// ----------------------------------------------------------------------------
const char* g_index_register_names[] =
{
	"d0",
	"d1",
	"d2",
	"d3",
	"d4",
	"d5",
	"d6",
	"d7",
	"a0",
	"a1",
	"a2",
	"a3",
	"a4",
	"a5",
	"a6",
	"a7",
	"pc",
	""
};

const char* g_control_register_names[ControlRegister::CR_COUNT] = 
{
	"?",
	"sfc",
	"dfc",
	"usp",
	"vbr",
	"cacr",
	"caar",
	"msp",
	"isp"
};

static const char* g_movem_reg_names[] =
{
	"d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
	"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"
};

static const char* g_scale_names[] =
{
	"",
	"*2",
	"*4",
	"*8"
};

#define ARRAY_SIZE(a)		 (sizeof(a) / sizeof(a[0]))
// ----------------------------------------------------------------------------
const char* get_opcode_string(Opcode opcode)
{
	if (opcode < ARRAY_SIZE(g_opcode_names))
		return g_opcode_names[opcode];
	return "?";
}

// ----------------------------------------------------------------------------
const char* get_index_register_string(IndexRegister reg)
{
	if (reg < ARRAY_SIZE(g_index_register_names))
		return g_index_register_names[reg];
	return "?";
}

// ----------------------------------------------------------------------------
const char* get_control_register_string(ControlRegister reg)
{
	if (reg < ARRAY_SIZE(g_control_register_names))
		return g_control_register_names[reg];
	return "?";
}

// ----------------------------------------------------------------------------
const char* get_suffix_string(Suffix suffix)
{
	switch (suffix)
	{
		case Suffix::BYTE:		return ".b";
		case Suffix::WORD:		return ".w";
		case Suffix::LONG:		return ".l";
		case Suffix::SHORT:		return ".s";
		default:
			break;
	}
	return "";
}

// ----------------------------------------------------------------------------
const char* get_movem_reg_string(uint16_t movem_reg)
{
	if (movem_reg < ARRAY_SIZE(g_movem_reg_names))
		return g_movem_reg_names[movem_reg];
	return "?";
}

// ----------------------------------------------------------------------------
const char* get_scale_shift_string(uint16_t scale_shift)
{
	if (scale_shift < ARRAY_SIZE(g_scale_names))
		return g_scale_names[scale_shift];
	return "?";
}

// ----------------------------------------------------------------------------
instruction::instruction() :
	header(0U),
	byte_count(0U),
	opcode(Opcode::NONE),
	suffix(Suffix::BYTE)
{
}

// ----------------------------------------------------------------------------
void instruction::reset()
{
	// This effectively sets the instruction to a "dc.w" statement
	byte_count = 2U;
	opcode = Opcode::NONE;
	suffix = Suffix::NONE;
	op0.type = OpType::INVALID;
	op1.type = OpType::INVALID;
	op2.type = OpType::INVALID;
	bf0.valid = 0;
	bf1.valid = 0;
}

}
