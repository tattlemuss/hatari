#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <cstdint>

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
	BGE,
	BGT,
	BHI,
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
	CHK,
	CLR,
	CMP,
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
	DIVU,
	EOR,
	EORI,
	EXG,
	EXT,
	ILLEGAL,
	JMP,
	JSR,
	LEA,
	LINK,
	LSL,
	LSR,
	MOVE,
	MOVEA,
	MOVEM,
	MOVEP,
	MOVEQ,
	MULS,
	MULU,
	NBCD,
	NEG,
	NEGX,
	NOP,
	NOT,
	OR,
	ORI,
	PEA,
	RESET,
	ROL,
	ROR,
	ROXL,
	ROXR,
	RTE,
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
	TRAPV,
	TST,
	UNLK,

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
	D_DIRECT,			// Dn		 000 reg. number:Dn
	A_DIRECT,			// An		 001 reg. number:An
	INDIRECT,			// (An)	   010 reg. number:An
	INDIRECT_POSTINC,	// (An) +	 011 reg. number:An
	INDIRECT_PREDEC,	// – (An)	 100 reg. number:An
	INDIRECT_DISP,		// (d16,An)   101 reg. number:An
	INDIRECT_INDEX,		// (d8,An,Xn) 110 reg. number:An
	ABSOLUTE_WORD,		// (xxx).W	111 000
	ABSOLUTE_LONG,		// (xxx).L	111 001
	PC_DISP,			// (d16,PC)   111 010
	PC_DISP_INDEX,		// (d8,PC,Xn) 111 011
	IMMEDIATE,			// <data>	 111 100
	MOVEM_REG,			// mask of registers
	RELATIVE_BRANCH,	// +/- 32K branch, doesn't display (pc)
	// Specific registers
	SR,
	USP,
	CCR,
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
			uint8_t reg;
		} indirect_disp;

		struct
		{
			int8_t disp;
			uint8_t a_reg;
			uint8_t d_reg;
			bool is_long;
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
			int16_t disp;
		} pc_disp;

		struct
		{
			int8_t disp;
			uint8_t d_reg;
			bool is_long;
		} pc_disp_index;

		struct
		{
			uint16_t reg_mask;
		} movem_reg;

		struct
		{
			int16_t disp;
		} relative_branch;

	};
};

enum class Suffix
{
	BYTE,
	WORD,
	LONG,
	SHORT,
	NONE
};

// ----------------------------------------------------------------------------
// A decoded instruction split into its constituent parts.
struct instruction
{
	instruction() :
		header(0U),
		byte_count(0U),
		opcode(Opcode::NONE),
		suffix(Suffix::BYTE)
	{
	}

	uint16_t	header;			// first 16-bit word of data
	uint16_t	byte_count;
	Opcode		opcode;
	Suffix		suffix;
	operand		op0;
	operand		op1;
};

#endif
