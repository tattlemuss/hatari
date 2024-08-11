#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <cstdint>

namespace hopper68
{
// ----------------------------------------------------------------------------
//	INSTRUCTION STORAGE
// ----------------------------------------------------------------------------
enum Opcode
{
	NONE,
	ABCD,
	ADD,
	ADDA,
	ADDI,
	ADDQ,
	ADDX,
	AND,
	ANDI,
	ASL,
	ASR,
	BCC,
	BCHG,
	BCLR,
	BCS,
	BEQ,
	BFCHG,
	BFCLR,
	BFEXTS,
	BFEXTU,
	BFFFO,
	BFINS,
	BFSET,
	BFTST,
	BGE,
	BGT,
	BHI,
	BKPT,
	BLE,
	BLS,
	BLT,
	BMI,
	BNE,
	BPL,
	BRA,
	BSET,
	BSR,
	BTST,
	BVC,
	BVS,
	CALLM,
	CAS,
	CAS2,
	CHK,
	CHK2,
	CLR,
	CMP,
	CMP2,
	CMPI,
	CMPA,
	CMPM,
	DBCC,
	DBCS,
	DBEQ,
	DBF,		// aka DBRA
	DBGE,
	DBGT,
	DBHI,
	DBLE,
	DBLS,
	DBLT,
	DBMI,
	DBNE,
	DBPL,
	DBVC,
	DBVS,
	DIVS,
	DIVSL,
	DIVU,
	DIVUL,
	EOR,
	EORI,
	EXG,
	EXT,
	EXTB,
	ILLEGAL,
	JMP,
	JSR,
	LEA,
	LINK,
	LSL,
	LSR,
	MOVE,
	MOVEA,
	MOVEC,
	MOVEM,
	MOVEP,
	MOVEQ,
	MOVES,
	MULS,
	MULU,
	NBCD,
	NEG,
	NEGX,
	NOP,
	NOT,
	OR,
	ORI,
	PACK,
	PEA,
	RESET,
	ROL,
	ROR,
	ROXL,
	ROXR,
	RTD,
	RTE,
	RTM,
	RTR,
	RTS,
	SBCD,
	SCC,
	SCS,
	SEQ,
	SF,
	SGE,
	SGT,
	SHI,
	SLE,
	SLS,
	SLT,
	SMI,
	SNE,
	SPL,
	ST,
	STOP,
	SUB,
	SUBA,
	SUBI,
	SUBQ,
	SUBX,
	SVC,
	SVS,
	SWAP,
	TAS,
	TRAP,
	TRAPCC,
	TRAPCS,
	TRAPEQ,
	TRAPF,
	TRAPGE,
	TRAPGT,
	TRAPHI,
	TRAPLE,
	TRAPLS,
	TRAPLT,
	TRAPMI,
	TRAPNE,
	TRAPPL,
	TRAPT,
	TRAPV,
	TRAPVC,
	TRAPVS,
	TST,
	UNLK,
	UNPK,

	COUNT	// Used for array sizes
};

enum class Size
{
	BYTE,
	WORD,
	LONG,
	NONE
};

// Operand types
enum OpType
{
	INVALID = 0,
	D_DIRECT,				// Dn		 000 reg. number:Dn
	A_DIRECT,				// An		 001 reg. number:An
	INDIRECT,				// (An)	   010 reg. number:An
	INDIRECT_POSTINC,		// (An) +	 011 reg. number:An
	INDIRECT_PREDEC,		// – (An)	 100 reg. number:An
	INDIRECT_DISP,			// (d16,An)   101 reg. number:An
	INDIRECT_INDEX,			// (d8,An,Xn) 110 reg. number:An
	ABSOLUTE_WORD,			// (xxx).W	111 000
	ABSOLUTE_LONG,			// (xxx).L	111 001
	PC_DISP,				// (d16,PC)   111 010
	PC_DISP_INDEX,			// (d8,PC,Xn) 111 011
	IMMEDIATE,				// <data>	 111 100
	MOVEM_REG,				// mask of registers
	RELATIVE_BRANCH,		// +/- 32K branch, doesn't display (pc)
	INDIRECT_PREINDEXED,	// (68020+)
	INDIRECT_POSTINDEXED,	// (68020+)
	MEMORY_INDIRECT,		// (68020+)
	NO_MEMORY_INDIRECT,		// (68020+)
	D_REGISTER_PAIR,		// (68020+)  "d0:d1"
	INDIRECT_REGISTER_PAIR,	// (68020+)  "Rn:Rn"

	// Specific registers
	SR,
	USP,
	CCR,
	CONTROL_REGISTER		// (68010+), used in movec
};

// ----------------------------------------------------------------------------
enum ControlRegister
{
	CR_UNKNOWN,
	CR_SFC,		// 68010+
	CR_DFC,		// 68010+
	CR_USP,		// 68010+
	CR_VBR,		// 68010+
	CR_CACR,	// 68020+
	CR_CAAR,	// 68020,68030
	CR_MSP,		// 68020+
	CR_ISP,		// 68020+
	CR_COUNT
};

// ----------------------------------------------------------------------------
// Any register used for indexing in indexed addressing modes.
enum IndexRegister
{
	INDEX_REG_D0,
	INDEX_REG_D1,
	INDEX_REG_D2,
	INDEX_REG_D3,
	INDEX_REG_D4,
	INDEX_REG_D5,
	INDEX_REG_D6,
	INDEX_REG_D7,
	INDEX_REG_A0,
	INDEX_REG_A1,
	INDEX_REG_A2,
	INDEX_REG_A3,
	INDEX_REG_A4,
	INDEX_REG_A5,
	INDEX_REG_A6,
	INDEX_REG_A7,
	INDEX_REG_PC,
	INDEX_REG_NONE
};

// ----------------------------------------------------------------------------
// Fields relating to address- or PC-index indirect (simple indexing)
// e.g. the "d2.w*4" in an opcode
struct index_indirect
{
	bool is_long;
	uint8_t scale_shift;		// 0 - scale 1, 1 = scale*2, 2 = scale*4, 3=scale*8
	IndexRegister index_reg;	// e.g. a0, d0, pc, none
};

// ----------------------------------------------------------------------------
struct indirect_index_full
{
	uint8_t					used[4];			// Denotes whether the below items are to be displayed
	int32_t					base_displacement;	// 0
	IndexRegister			base_register;		// 1
	index_indirect			index;				// 2
	int32_t					outer_displacement;	// 3
};

// ----------------------------------------------------------------------------
struct operand
{
	operand() :
		type(OpType::INVALID)
	{}

	// The overall type of operand
	OpType				type;

	// Data fields, dependent on OpType above.
	// This is a rather ugly union, but the simplest way to store the data
	// consistently without a lot of awkward subclassing.
	union
	{
		struct
		{
			Size		size;		// byte, word or long
			uint32_t	val0;
			bool		is_signed;
		} imm;

		struct
		{
			uint8_t	reg;			// e.g. D3
		} d_register;

		struct
		{
			uint8_t reg;
		} a_register;

		struct
		{
			uint8_t reg;
		} indirect;

		struct
		{
			uint8_t reg;
		} indirect_postinc;

		struct
		{
			uint8_t reg;
		} indirect_predec;

		struct
		{
			int16_t disp;
			uint8_t reg;						// Address register only
		} indirect_disp;

		struct
		{
			int8_t disp;						// displacement [-128...127)
			uint8_t a_reg;						// base register
			index_indirect indirect_info;
		} indirect_index;

		struct
		{
			uint32_t wordaddr;
		} absolute_word;

		struct
		{
			uint32_t longaddr;
		} absolute_long;

		struct
		{
			// Base register is implicitly "PC"
			int32_t inst_disp;					// offset from the base instruction address. Can be $7ffe+6 bytes max.
		} pc_disp;

		struct
		{
			// Base register is implicitly "PC"
			int32_t inst_disp;					// offset from the base instruction address. Can be $7ffe+6 bytes max.
			index_indirect indirect_info;
		} pc_disp_index;

		struct
		{
			uint16_t reg_mask;
		} movem_reg;

		struct
		{
			int32_t inst_disp;		// offset from the base instruction address. Can be $7ffe+2 bytes max.
		} relative_branch;

		// Used in:
		// INDIRECT_PREINDEXED
		// INDIRECT_POSTINDEXED
		// MEMORY_INDIRECT
		// NO_MEMORY_INDIRECT
		indirect_index_full		indirect_index_68020;

		struct
		{
			ControlRegister cr;
		} control_register;

		struct
		{
			uint8_t dreg1;
			uint8_t dreg2;
		} d_register_pair;

		struct
		{
			IndexRegister reg1;
			IndexRegister reg2;
		} indirect_register_pair;
		
	};
};

// offset/width for 68020 bitfield instructions.
struct bitfield
{
	uint8_t valid;
	uint8_t	offset_is_dreg;		// e.g "d3"
	uint8_t	offset;				// value 0-31, or register 0-7
	uint8_t	width_is_dreg;
	uint8_t	width;				// value 0-31, or register 0-7
};

// ----------------------------------------------------------------------------
enum class Suffix
{
	BYTE,
	WORD,
	LONG,
	SHORT,
	NONE
};

// ----------------------------------------------------------------------------
// Helper functions to convert parts of the instruction to strings.
extern const char* get_opcode_string(Opcode opcode);
extern const char* get_index_register_string(IndexRegister reg);
extern const char* get_control_register_string(ControlRegister reg);
extern const char* get_suffix_string(Suffix suffix);
// Convert from MOVEM register number to a string
extern const char* get_movem_reg_string(uint16_t movem_reg);
extern const char* get_scale_shift_string(uint16_t scale_shift);

// ----------------------------------------------------------------------------
// A decoded instruction split into its constituent parts.
struct instruction
{
	instruction();
	void reset();

	uint32_t	address;
	uint16_t	header;			// first 16-bit word of data
	uint16_t	byte_count;		// total number of bytes in the instruction
	Opcode		opcode;
	Suffix		suffix;

	// operands and matching bitfields, when applicable
	operand		op0;
	bitfield	bf0;
	operand		op1;
	bitfield	bf1;
	operand		op2;
};

}
#endif
