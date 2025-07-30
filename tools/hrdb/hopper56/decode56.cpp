#include "decode56.h"

#include "buffer56.h"
#include "instruction56.h"

/*
	decode.cpp - converts binary data into a tokenized format which is easy to
	print and analyse.

	56000 opcodes fall into 2 groups: ones that include up to 2 "parallel moves",
	and the standard opcodes. The top 4 bits are generally 0000 for the
	non-parallel moves (although there are exceptions where the parallel move
	types can contain 0000 there)

	Parallel moves are in a 24:8 bit format where 24 bits describe the parallel parts.
	We decode the parallel part in decode_pm, and use a table lookup for the bottom
	8 bits (in pmove_table.i) where every opcode and operand is fixed.

	Non-parallel moves are more complex. We use bits 14-19 inclusive (6 bits) for
	another set of lookup table functions (nonp_tables.i) and dispatch to one of
	27 decoder functions. These tables (and the signatures for the decoder functions
	are generated from a script which reads the 'm56k.mch' opcode description file
	from the rmac assembler (slightly edited to help the script.)

	The decoder functions are grouped and keye on the pattern of variable fields
	in the opcode, e.g. "decode_rrr" is for all opcodes that contain a single 3-bit
	"rrr" field which might describe e.g. a R0-7 register. This helps group the
	decoders into similar opcodes, such as all similar jump instructions.
*/

#define H56CHECK(x)		if (x) return 1;

namespace hop56
{
	// Converts bit IDs in the parallel moves, to full register IDs
	// NOTE: this is the first 32 entries of registers_triple_bit
	static Reg pmove_registers_1[32] =
	{
		Reg::NONE, Reg::NONE, Reg::NONE, Reg::NONE,
		Reg::X0, Reg::X1, Reg::Y0, Reg::Y1,
		Reg::A0, Reg::B0, Reg::A2, Reg::B2, Reg::A1, Reg::B1, Reg::A,  Reg::B,
	 	Reg::R0, Reg::R1, Reg::R2, Reg::R3, Reg::R4, Reg::R5, Reg::R6, Reg::R7,
		Reg::N0, Reg::N1, Reg::N2, Reg::N3,	Reg::N4, Reg::N5, Reg::N6, Reg::N7
	};
	static Reg pmove_registers_bank_x[4] =
	{
		Reg::X0, Reg::X1, Reg::A, Reg::B
	};
	static Reg pmove_registers_bank_y[4] =
	{
		Reg::Y0, Reg::Y1, Reg::A, Reg::B
	};
	static Reg pmove_registers_movel[8] =
	{
		Reg::A10, Reg::B10,
		Reg::X, Reg::Y, Reg::A, Reg::B,
		Reg::AB, Reg::BA,
	};
	static Reg registers_jj[4] =
	{
		Reg::X0, Reg::Y0, Reg::X1, Reg::Y1
	};
	static Reg registers_a_or_b[2] =
	{
		Reg::A, Reg::B
	};
	static Reg registers_dd[4] =
	{
		Reg::X0, Reg::X1, Reg::Y0, Reg::Y1
	};

	// See "Table A-18 Triple-Bit Register Encodings"
	// Plus there is a better list on p520, in the REP description.
	static Reg registers_triple_bit[64] =
	{
		// 	0 0 0	4 registers in Data ALU	(DD)
		// NOTE: not used
		Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::X0,	Reg::X1,	Reg::Y0,	Reg::Y1,
		// 	0 0 1		8 accumulators in Data ALU	(DDD)
		Reg::A0,	Reg::B0,	Reg::A2,	Reg::B2,	Reg::A1,	Reg::B1,	Reg::A,		Reg::B,
		// 	0 1 0		8 address registers in AGU	(TTT)
		Reg::R0,	Reg::R1,	Reg::R2,	Reg::R3,	Reg::R4,	Reg::R5,	Reg::R6,	Reg::R7,
		// 	0 1 1 		8 address offset registers in AGU (NNN)
		Reg::N0,	Reg::N1,	Reg::N2,	Reg::N3,	Reg::N4,	Reg::N5,	Reg::N6,	Reg::N7,
		//	1 0 0		8 address modifier registers in AGU	(FFF)
		Reg::M0,	Reg::M1,	Reg::M2,	Reg::M3,	Reg::M4,	Reg::M5,	Reg::M6,	Reg::M7,
		// 	1 0 1		n/a
		Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,
		//	1 1 0		n/a
		Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,
		//	1 1 1		8 program controller registers	(GGG)
		Reg::NONE,	Reg::SR,	Reg::OMR,	Reg::SP,	Reg::SSH,	Reg::SSL,	Reg::LA,	Reg::LC
	};

	// Special registers in the movec instruction
	static Reg registers_movec[32] =
	{
		Reg::M0,	Reg::M1,	Reg::M2,	Reg::M3,	Reg::M4,	Reg::M5,	Reg::M6,	Reg::M7,
		Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,
		Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,
		Reg::NONE,	Reg::SR,	Reg::OMR,	Reg::SP,	Reg::SSH,	Reg::SSL,	Reg::LA,	Reg::LC
	};

	static Reg registers_movem[64] =
	{
		Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,
		Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,
		Reg::R0,	Reg::R1,	Reg::R2,	Reg::R3,	Reg::R4,	Reg::R5,	Reg::R6,	Reg::R7,
		Reg::N0,	Reg::N1,	Reg::N2,	Reg::N3,	Reg::N4,	Reg::N5,	Reg::N6,	Reg::N7,
		Reg::M0,	Reg::M1,	Reg::M2,	Reg::M3,	Reg::M4,	Reg::M5,	Reg::M6,	Reg::M7,
		Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,
		Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,	Reg::NONE,
		Reg::NONE,	Reg::SR,	Reg::OMR,	Reg::SP,	Reg::SSH,	Reg::SSL,	Reg::LA,	Reg::LC
	};

	// Workspace to decode a non-parallel-move opcode
	struct nonp_context
	{
		nonp_context(instruction& _inst, buffer_reader& _buf,
			const decode_settings& _settings) :
			inst(_inst),
			buf(_buf),
			settings(_settings)
		{
		}
		uint32_t			header;
		instruction&		inst;
		buffer_reader&		buf;
		const decode_settings& settings;
	};

	// ========================================================================
	//	OPCODE TABLE FOR PARALLEL MOVE INSTRUCTIONS
	// ========================================================================

	struct pmove_entry
	{
		Opcode opcode;
		uint8_t				neg;
		Reg					regs[3];
	};

	#define PM_ENTRY(opcode, neg, reg0, reg1, reg2)		\
		{ Opcode::opcode, neg, { Reg::reg0, Reg::reg1, Reg::reg2 } }

#include "pmove_table.i"

	// ========================================================================
	// Setters for operands

	// Create R-register from 0-7 index
	Reg make_r(uint32_t r_num) { return (Reg)(Reg::R0 + r_num); }

	// Create N-register from 0-7 index
	Reg make_n(uint32_t n_num) { return (Reg)(Reg::N0 + n_num); }

	// Set register. Return 1 if register is invalid.
	static int set_reg(operand& op, Reg reg)
	{
		op.type = operand::REG; op.reg.index = reg;
		return reg == Reg::NONE;
	}
	// This sets a signed value
	static void set_imm_short(operand& op, int8_t val)
	{
		op.type = operand::IMM_SHORT; op.imm_short.val = val;
	}
	static void set_postdec_offset(operand& op, uint32_t reg_num)
	{
		op.type = operand::POSTDEC_OFFSET;
		op.postdec_offset.index_1 = make_r(reg_num);
		op.postdec_offset.index_2 = make_n(reg_num);
	}
	static void set_postinc_offset(operand& op, uint32_t reg_num)
	{
		op.type = operand::POSTINC_OFFSET;
		op.postinc_offset.index_1 = make_r(reg_num);
		op.postinc_offset.index_2 = make_n(reg_num);
	}
	static void set_postdec(operand& op, uint32_t reg_num)
	{
		op.type = operand::POSTDEC; op.postdec.index = make_r(reg_num);
	}
	static void set_postinc(operand& op, uint32_t reg_num)
	{
		op.type = operand::POSTINC; op.postdec.index = make_r(reg_num);
	}
	static void set_index_offset(operand& op, uint32_t reg_num)
	{
		op.type = operand::INDEX_OFFSET;
		op.postinc_offset.index_1 = make_r(reg_num);
		op.postinc_offset.index_2 = make_n(reg_num);
	}
	static void set_no_update(operand& op, uint32_t reg_num)
	{
		op.type = operand::NO_UPDATE; op.no_update.index = make_r(reg_num);
	}
	static void set_predec(operand& op, uint32_t reg_num)
	{
		op.type = operand::PREDEC; op.predec.index = make_r(reg_num);
	}
	static void set_abs(operand& op, uint32_t addr)
	{
		op.type = operand::ABS; op.abs.address = addr;
	}
	static void set_abs_short(operand& op, uint8_t addr)
	{
		op.type = operand::ABS_SHORT; op.abs_short.address = addr;
	}
	static void set_io_short(operand& op, uint8_t addr)
	{
		// "The I/O short address is ones extended to 16 bits
		// to address the I/O portion of X and Y memory (addresses
		// $FFC0–$FFFF – see Figure 6-12)."
		// This takes a 6-bit "pppppp" address
		op.type = operand::IO_SHORT;
		op.io_short.address = 0xffc0 | addr;
	}
	static void set_imm(operand& op, uint32_t val)
	{
		op.type = operand::IMM; op.imm.val = val;
 	}
	static void allocate_operands(pmove& pmove, uint32_t w, operand*& opA, operand*& opB)
	{
		// Choose src/dest for "w" modes,
		operand& op0 = pmove.operands[0];
		operand& op1 = pmove.operands[1];
		opA = (w == 0) ? &op1 : &op0;
		opB = (w == 0) ? &op0 : &op1;
	}
	static void allocate_operands(instruction& inst, uint32_t w, operand*& opA, operand*& opB)
	{
		// Choose src/dest for "w" modes,
		operand& op0 = inst.operands[0];
		operand& op1 = inst.operands[1];
		opA = (w == 0) ? &op1 : &op0;
		opB = (w == 0) ? &op0 : &op1;
	}

	// ========================================================================
	//	OPCODE SHARED FUNCTIONS
	// ========================================================================

	// Speciifies the set of allowable addressing modes in parallel moves.
	// The full set of modes (EA_MODE_ALL) is
	// 		(Rn)-Nn		0 0 0 n n n
	// 		(Rn)+Nn		0 0 1 n n n
	// 		(Rn)-		0 1 0 n n n
	// 		(Rn)+		0 1 1 n n n
	// 		(Rn)		1 0 0 n n n
	// 		(Rn+Nn)		1 0 1 n n n
	// 		-(Rn)		1 1 1 n n n
	// 		Abs-addr	1 1 0 0 0 0
	// 		Immediate	1 1 0 1 0 0			; not when dest operand!
	// The other modes are subsets of the full set.
	enum EA_MODE
	{
		EA_MODE_ALL,		// allows immediate
		EA_MODE_ABS,		// registers + absolute address, no immediate
		EA_MODE_REGS,		// only R-register-based (Rn etc)
		EA_MODE_LUA,		// only the first 4 entries (special case)
		EA_MODE_COUNT
	};

	// Handle "mmm_rrr" addressing mode and register for Rn/abs/immediate addressing in parallel moves
	static int decode_mmmrrr(operand& op, Memory mem,
		uint32_t mmm, uint32_t rrr, EA_MODE mode,
		buffer_reader& buf)
	{
		// Special case: LUA only allows 4 modes
		if (mode == EA_MODE_LUA && mmm > 3)
			return 1;

		uint32_t next = 0;
		op.memory = mem;
		switch (mmm)
		{
			case 0:		set_postdec_offset(op, rrr); return 0;
			case 1:		set_postinc_offset(op, rrr); return 0;
			case 2:		set_postdec(op, rrr); return 0;
			case 3:		set_postinc(op, rrr); return 0;
			case 4:		set_no_update(op, rrr); return 0;
			case 5: 	set_index_offset(op, rrr); return 0;
			case 7:		set_predec(op, rrr); return 0;
			case 6:
				// Immediate mode only in "full"
				if ((rrr == 0x4) && (mode == EA_MODE_ALL))
				{
					H56CHECK(buf.read_word(next))
					set_imm(op, next);
					op.memory = MEM_NONE;
					return 0;
				}
				else if ((rrr == 0x0) &&
							((mode == EA_MODE_ALL) ||
							 (mode == EA_MODE_ABS)))
				{
					// Absolute address
					H56CHECK(buf.read_word(next))
					set_abs(op, next);
					return 0;
				}
				return 1;
			default:
				return 1;
		} // switch
		return 1;
	}

	// Decode short-format "mm rrr" fields used in XY parallel data moves.
	// NOTE: this is different to LUA "mmmrrr" mode.
	static int decode_mmrrr(operand& op, Memory mem, uint32_t mm, uint32_t rrr, buffer_reader& /*buf*/)
	{
		op.memory = mem;
		switch (mm)
		{
			case 1:		set_postinc_offset(op, rrr); return 0;
			case 2:		set_postdec(op, rrr); return 0;
			case 3:		set_postinc(op, rrr); return 0;
			case 0:		set_no_update(op, rrr); return 0;
		}
		return 1;
	}

	// Decode all parallel moves in an instruction.
	static int decode_pmove(instruction& inst, uint32_t header, const decode_settings&, buffer_reader& buf)
	{
		uint32_t pdata = (header >> 8) & 0xffff;

		// UM, page A-162 onwards

		// Pre-choose operand order for read vs write
		uint32_t w = (pdata >> 7) & 0x1;

		// Pre-choose modes and registers
		uint32_t mmm = (pdata >> 3) & 0x7;
		uint32_t rrr = (pdata >> 0) & 0x7;
		EA_MODE mode = w ? EA_MODE_ALL : EA_MODE_ABS;	// here w==0 means "reading from reg, and writing to memory"

		// Prep the most commonly-used operand slots/arguments
		operand& op0 = inst.pmoves[0].operands[0];
		operand& op1 = inst.pmoves[0].operands[1];
		operand* opA, * opB;

		if (pdata == 0x2000)
			return 0;			// No Parallel Move
		if ((pdata & 0xffe0) == 0x2040)
		{
			// Address Reg Update (one operand)
			uint32_t mm = (pdata >> 3) & 3;
			switch (mm)
			{
				case 0: set_postdec_offset(op0, rrr); break;
				case 1: set_postinc_offset(op0, rrr); break;
				case 2: set_postdec(op0, rrr); break;
				case 3: set_postinc(op0, rrr); break;
			}
			return 0;
		}
		if ((pdata & 0xf440) == 0x4040)
		{
			// L: Memory move
			// 0100 L0LL W1MM MRRR format
			// 1111 0100 0100 0000 mask
			// 0100 0000 0100 0000 val
			allocate_operands(inst.pmoves[1], w, opA, opB);
			uint32_t LLL = ((pdata >> 8) & 0x3) | ((pdata >> 9) & 0x4);
			H56CHECK(set_reg(*opB, pmove_registers_movel[LLL]))
			return decode_mmmrrr(*opA, MEM_L, mmm, rrr, EA_MODE_ABS, buf);
		}
		if ((pdata & 0xf040) == 0x1000)
		{
			// X: Memory and Reg Move, Class I
			// 0001 ffdf W0MM MRRR format
			// 1111 0000 0100 0000 mask
			// 0001 0000 0000 0000 val
			allocate_operands(inst.pmoves[0], w, opA, opB);
			uint32_t f = (pdata >> 8) & 0x1;		// S2/D2
			uint32_t d = (pdata >> 9) & 0x1;
		   	uint32_t ff = (pdata >> 10) & 0x3;		// S1/D1
			H56CHECK(set_reg(*opB, pmove_registers_bank_x[ff]))
			H56CHECK(decode_mmmrrr(*opA, MEM_X, mmm, rrr, mode, buf))

			// Second pmove is limited
			H56CHECK(set_reg(inst.pmoves[1].operands[0], d ? Reg::B  : Reg::A))
			H56CHECK(set_reg(inst.pmoves[1].operands[1], f ? Reg::Y1 : Reg::Y0))
			return 0;
		}
		if ((pdata & 0xf040) == 0x1040)
		{
			// Y: Reg and Memory Move, Class I
			// 0001 deff W1MM MRRR format
			// 1111 0000 0100 0000 mask
			// 0001 0000 0100 0000 val
			allocate_operands(inst.pmoves[1], w, opA, opB);
		   	uint32_t ff = (pdata >> 8) & 0x3;		// S1/D1
			uint32_t e = (pdata >> 10) & 0x1;		// S2/D2
			uint32_t d = (pdata >> 11) & 0x1;
			H56CHECK(set_reg(*opB, pmove_registers_bank_y[ff]))
			H56CHECK(decode_mmmrrr(*opA, MEM_Y, mmm, rrr, mode, buf))

			// Second pmove is limited
			H56CHECK(set_reg(inst.pmoves[0].operands[0], d ? Reg::B  : Reg::A))
			H56CHECK(set_reg(inst.pmoves[0].operands[1], e ? Reg::X1 : Reg::X0))
			return 0;
		}
		if ((pdata & 0xfec0) == 0x0800)
		{
			// X: Memory and Reg Move, Class II
			// 0000 100d 00MM MRRR format
			// 1111 1110 1100 0000 mask
			// 0000 1000 0000 0000 val
			uint32_t d = (pdata >> 8) & 0x1;
			Reg accum_reg = registers_a_or_b[d];
			H56CHECK(set_reg(op0, accum_reg))
			H56CHECK(decode_mmmrrr(op1, MEM_X, mmm, rrr, EA_MODE_REGS, buf))		// this is limited to register modes

			// Second pmove is always X0
			H56CHECK(set_reg(inst.pmoves[1].operands[0], Reg::X0))
			H56CHECK(set_reg(inst.pmoves[1].operands[1], accum_reg))
			return 0;
		}
		if ((pdata & 0xfec0) == 0x0880)
		{
			// Y: Reg and Memory Move, Class II
			// 0000 100d 10MM MRRR format
			// 1111 1110 1100 0000 mask
			// 0000 1000 1000 0000 val
			uint32_t d = (pdata >> 8) & 0x1;
			Reg accum_reg = registers_a_or_b[d];
			H56CHECK(set_reg(inst.pmoves[1].operands[0], accum_reg))
			H56CHECK(decode_mmmrrr(inst.pmoves[1].operands[1], MEM_Y, mmm, rrr, EA_MODE_REGS, buf))		// this is limited to short modes

			// Second pmove is always Y0
			H56CHECK(set_reg(inst.pmoves[0].operands[0], Reg::Y0))
		   	H56CHECK(set_reg(inst.pmoves[0].operands[1], accum_reg))
			return 0;
		}
		if ((pdata & 0xc840) == 0x4000)
		{
			// X: memory from short absolute
			allocate_operands(inst.pmoves[0], w, opA, opB);
			uint32_t ddddd = ((pdata >> 8) & 0x7) | ((pdata >> 9) & 0x18);
			uint32_t ea = (pdata >> 0) & 0x3f;
			opA->memory = MEM_X;
			set_abs_short(*opA, ea);
			H56CHECK(set_reg(*opB, pmove_registers_1[ddddd]))
			return 0;
		}
		if ((pdata & 0xc840) == 0x4040)
		{
			// X: memory move
			// 01dd 0ddd W1MM MRRR format
			// 1100 1000 0100 0000 mask
			// 0100 0000 0100 0000 val
			allocate_operands(inst.pmoves[0], w, opA, opB);
			uint32_t ddddd = ((pdata >> 8) & 0x7) | ((pdata >> 9) & 0x18);
			H56CHECK(set_reg(*opB, pmove_registers_1[ddddd]))
			return decode_mmmrrr(*opA, MEM_X, mmm, rrr, mode, buf);
		}
		if ((pdata & 0xc840) == 0x4840)
		{
			// Y: Memory move
			// 01dd 1ddd W1MM MRRR format
			// 1100 1000 0100 0000 mask
			// 0100 1000 0100 0000 val
			allocate_operands(inst.pmoves[1], w, opA, opB);
			uint32_t ddddd = ((pdata >> 8) & 0x7) | ((pdata >> 9) & 0x18);
			H56CHECK(set_reg(*opB, pmove_registers_1[ddddd]))
			return decode_mmmrrr(*opA, MEM_Y, mmm, rrr, mode, buf);
		}
		if ((pdata & 0xfc00) == 0x2000)
		{
			// Reg->Reg (special case of immediate short)
			uint32_t imrege = (pdata >> 5) & 0x1f;
			uint32_t imregd = (pdata >> 0) & 0x1f;
			H56CHECK(set_reg(op0, pmove_registers_1[imrege]))
			H56CHECK(set_reg(op1, pmove_registers_1[imregd]))
			return 0;
		}
		if ((pdata & 0xe000) == 0x2000)
		{
			// 6.3.5.3.3 Immediate short data move
			uint32_t imdata = (pdata & 0xff);
			uint32_t imreg = (pdata >> 8) & 0x1f;
			set_imm_short(op0, imdata);
			H56CHECK(set_reg(op1, pmove_registers_1[imreg]))
			return 0;
		}
		if ((pdata & 0xc8000) == 0x8000)
		{
			// XY Memory Data move
			// 1wmm eeff WrrM MRRR format
			// 1000 0000 0000 0000 mask
			// 1000 0000 0000 0000 val
			// X memory:
			uint32_t MM = (pdata >> 3) & 0x3;
			uint32_t ee = (pdata >> 10) & 0x3;
			allocate_operands(inst.pmoves[0], w, opA, opB);
			H56CHECK(set_reg(*opB, pmove_registers_bank_x[ee]))
			H56CHECK(decode_mmrrr(*opA, MEM_X, MM, rrr, buf))

			// Y memory:
			uint32_t mm = (pdata >> 12) & 0x3;
			uint32_t rr = (pdata >> 5) & 0x3;
			uint32_t ff = (pdata >> 8) & 0x3;
			rrr &= 4;					// isolate memory bank base (R0 or R4)
			rrr ^= 4;					// switch memory bank base
			rrr |= rr;
			w = (pdata >> 14) & 0x1;
			allocate_operands(inst.pmoves[1], w, opA, opB);
			H56CHECK(set_reg(*opB, pmove_registers_bank_y[ff]))
			H56CHECK(decode_mmrrr(*opA, MEM_Y, mm, rrr, buf))
		}
		return 0;
	}

	int decode_pm(instruction& inst, uint32_t header, const decode_settings& settings, buffer_reader& buf)
	{
		// Main instruction type is stored in the lower 8 bits
		uint8_t base_type = inst.header & 0xff;
		const pmove_entry& entry = g_pmove_entries[base_type];
		inst.opcode = entry.opcode;
		inst.word_count = 1;		// default values
		inst.neg_operands = entry.neg;
		// Copy the table entry registers into the final instruction
		// These do a NONE test so H56CHECK isn't needed
		if (entry.regs[0] != Reg::NONE)
			set_reg(inst.operands[0], entry.regs[0]);
		if (entry.regs[1] != Reg::NONE)
			set_reg(inst.operands[1], entry.regs[1]);
		if (entry.regs[2] != Reg::NONE)
			set_reg(inst.operands[2], entry.regs[2]);
		int ret = decode_pmove(inst, header, settings, buf);
		return ret;
	}

	// ========================================================================
	//	NON-PARALLEL MOVE OPCODE FUNCTIONS
	// ========================================================================
	static int decode_rep_loop_address(nonp_context& ctx)
	{
		uint32_t is_rep = (ctx.inst.header >> 5) & 0x1;
		if (!is_rep)
		{
			uint32_t addr;
			H56CHECK(ctx.buf.read_word(addr))

			// "The assembler calculates the end-of-loop address to be loaded into LA (the abso-
			// lute address extension word) by evaluating the end-of-loop expression “expr” and sub-
			// tracting one
			set_abs(ctx.inst.operands[1], (addr & 0xffff) + 1);
		}
		return 0;
	}

	// ------------------------------------------------------------------------
	static int decode_jcc_dest(nonp_context& ctx)
	{
		{
			// JSCLR/JSET/JSSET/JCLR needs extra dest words
			uint32_t dest;
			H56CHECK(ctx.buf.read_word(dest))
			set_abs(ctx.inst.operands[2], dest);
		}
		return 0;
	}

	// ------------------------------------------------------------------------
	// Decoder for field type '____________________'
	// This is the variant with no fields.
	static int decode_nonp_none(nonp_context& ctx, Opcode opcode)
	{
		ctx.inst.opcode = opcode;
		return 0;
	}

	// ------------------------------------------------------------------------
	// Decoder for field type '______________jjd___'
	// Used in 00011000000001jjd000  O_DIV      'dsp_x0y0ab jj=(x0=0, x1=2, y0=1, y1=3), d=(a=0, b=1)'
	static int decode_nonp_jjd(nonp_context& ctx, Opcode opcode)
	{
		uint32_t jj = (ctx.header >> 4) & 0x3;
		uint32_t d = (ctx.header >> 3) & 0x1;
		H56CHECK(set_reg(ctx.inst.operands[0], registers_jj[jj]))
		H56CHECK(set_reg(ctx.inst.operands[1], registers_a_or_b[d]))
		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '____iiiiiiii______ee'
	// Used in 0000iiiiiiii101110ee  O_ANDI     'dsp_immcr ee=(mr=0, ccr=1, omr=2)'
	// Used in 0000iiiiiiii111110ee  O_ORI      'dsp_immcr ee=(mr=0, ccr=1, omr=2)'
	static int decode_nonp_iiiiiiiiee(nonp_context& ctx, Opcode opcode)
	{
		uint32_t imm = (ctx.header >> 8) & 0xff;
		uint32_t ee = (ctx.header >> 0) & 0x3;
		// Use set_imm to show as unsigned, since this is a control reg
		set_imm(ctx.inst.operands[0], imm);
		switch (ee)
		{
			case 0: H56CHECK(set_reg(ctx.inst.operands[1], Reg::MR)); break;
			case 1: H56CHECK(set_reg(ctx.inst.operands[1], Reg::CCR)); break;
			case 2: H56CHECK(set_reg(ctx.inst.operands[1], Reg::OMR)); break;
			default: return 1;
		}
		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '___e_____ttt_jjjdTTT'
	// Used in 001e00000ttt0jjjdTTT  O_TCC      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e10000ttt0jjjdTTT  O_TCS      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e01010ttt0jjjdTTT  O_TEC      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e10100ttt0jjjdTTT  O_TEQ      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e11010ttt0jjjdTTT  O_TES      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e00010ttt0jjjdTTT  O_TGE      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e01110ttt0jjjdTTT  O_TGT      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e01100ttt0jjjdTTT  O_TLC      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e11110ttt0jjjdTTT  O_TLE      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e11100ttt0jjjdTTT  O_TLS      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e10010ttt0jjjdTTT  O_TLT      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e10110ttt0jjjdTTT  O_TMI      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e00100ttt0jjjdTTT  O_TNE      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e11000ttt0jjjdTTT  O_TNR      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e00110ttt0jjjdTTT  O_TPL      'dsp_baab + s1,d1 [s2,d2]'
	// Used in 001e01000ttt0jjjdTTT  O_TNN      'dsp_baab + s1,d1 [s2,d2]'
	static int decode_nonp_etttjjjdTTT(nonp_context& ctx, Opcode opcode)
	{
		// Actual encoding:
		// 00000010CCCC00000JJJD000
		// 00000011CCCC0ttt0JJJDTTT		for extra S2,D2 pair
		uint32_t jjjd = (ctx.header >> 3) & 0xf;
		Reg regA = Reg::NONE;
		Reg regB = Reg::NONE;
		switch (jjjd)
		{
			case 0x0:	regA = Reg::B;  regB = Reg::A; break;
			case 0x1:	regA = Reg::A;  regB = Reg::B; break;
			case 0x8:	regA = Reg::X0; regB = Reg::A; break;
			case 0x9:	regA = Reg::X0; regB = Reg::B; break;
			case 0xa:	regA = Reg::Y0; regB = Reg::A; break;
			case 0xb:	regA = Reg::Y0; regB = Reg::B; break;
			case 0xc:	regA = Reg::X1; regB = Reg::A; break;
			case 0xd:	regA = Reg::X1; regB = Reg::B; break;
			case 0xe:	regA = Reg::Y1; regB = Reg::A; break;
			case 0xf:	regA = Reg::Y1; regB = Reg::B; break;
			default: return 1;
		}
		H56CHECK(set_reg(ctx.inst.operands[0], regA))
		H56CHECK(set_reg(ctx.inst.operands[1], regB))

		if ((ctx.header >> 16) & 0x1)
		{
			// Extra Rn pair
			set_reg(ctx.inst.operands2[0], make_r((ctx.header >> 8) & 0x7));
			set_reg(ctx.inst.operands2[1], make_r((ctx.header >> 0) & 0x7));
		}

		ctx.inst.opcode = opcode;
		return 0;
	}


	// Decoder for field type '________aaaaaaaaaaaa'
	// Used in 11100000aaaaaaaaaaaa  O_JHS      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11101000aaaaaaaaaaaa  O_JCS      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11100101aaaaaaaaaaaa  O_JEC      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11101010aaaaaaaaaaaa  O_JEQ      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11101101aaaaaaaaaaaa  O_JES      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11100111aaaaaaaaaaaa  O_JGT      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11100110aaaaaaaaaaaa  O_JLC      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11101111aaaaaaaaaaaa  O_JLE      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11101110aaaaaaaaaaaa  O_JLS      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11101001aaaaaaaaaaaa  O_JLT      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11101011aaaaaaaaaaaa  O_JMI      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11100010aaaaaaaaaaaa  O_JNE      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11101100aaaaaaaaaaaa  O_JNR      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11100011aaaaaaaaaaaa  O_JPL      'dsp_abs12 + Jcc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11000000aaaaaaaaaaaa  O_JMP      'dsp_abs12 + JMP xxx aaaaaaaaaaaa=12bit address'
	// Used in 11110000aaaaaaaaaaaa  O_JSHS     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11111000aaaaaaaaaaaa  O_JSCS     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11110101aaaaaaaaaaaa  O_JSEC     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11111010aaaaaaaaaaaa  O_JSEQ     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11111101aaaaaaaaaaaa  O_JSES     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11110001aaaaaaaaaaaa  O_JSGE     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11110111aaaaaaaaaaaa  O_JSGT     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11110110aaaaaaaaaaaa  O_JSLC     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11111111aaaaaaaaaaaa  O_JSLE     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11111110aaaaaaaaaaaa  O_JSLS     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11111001aaaaaaaaaaaa  O_JSLT     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11111011aaaaaaaaaaaa  O_JSMI     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11110010aaaaaaaaaaaa  O_JSNE     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11111100aaaaaaaaaaaa  O_JSNR     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11110011aaaaaaaaaaaa  O_JSPL     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11110100aaaaaaaaaaaa  O_JSNN     'dsp_abs12 + JScc xxx aaaaaaaaaaaa=12bit address'
	// Used in 11010000aaaaaaaaaaaa  O_JSR      'dsp_abs12 + JSR xxx aaaaaaaaaaaa=12bit address'
	static int decode_nonp_aaaaaaaaaaaa(nonp_context& ctx, Opcode opcode)
	{
		uint32_t addr = ctx.header & 0xfff;
		set_abs(ctx.inst.operands[0], addr);
		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '______mmmrrr________'
	// Used in 101011mmmrrr10100000  O_JHS      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10101000  O_JCS      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10100101  O_JEC      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10101010  O_JEQ      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10101101  O_JES      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10100001  O_JES      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10100111  O_JGT      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10100110  O_JLC      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10101111  O_JLE      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10101110  O_JLS      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10101001  O_JLT      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10101011  O_JMI      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10100010  O_JNE      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10101100  O_JNR      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10100011  O_JPL      'dsp_ea Jcc ea mmmrrr=ea'
	// Used in 101011mmmrrr10000000  O_JMP      'dsp_ea JMP ea (+optional 24bit address)'
	// Used in 101111mmmrrr10100000  O_JSHS     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10101000  O_JSCS     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10100101  O_JSEC     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10101010  O_JSEQ     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10101101  O_JSES     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10100001  O_JSGE     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10100111  O_JSGT     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10100110  O_JSLC     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10101111  O_JSLE     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10101110  O_JSLS     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10101001  O_JSLT     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10101011  O_JSMI     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10100010  O_JSNE     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10101100  O_JSNR     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10100011  O_JSPL     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10100100  O_JSNN     'dsp_ea JScc ea mmmrrr=ea (+optional 24bit address)'
	// Used in 101111mmmrrr10000000  O_JSR      'dsp_ea JSR ea mmmrrr=ea (+optional 24bit address)'
	static int decode_nonp_mmmrrr(nonp_context& ctx, Opcode opcode)
	{
		uint32_t mmm = (ctx.header >> 11) & 0x7;
		uint32_t rrr = (ctx.header >>  8) & 0x7;
		H56CHECK(decode_mmmrrr(ctx.inst.operands[0], MEM_NONE, mmm, rrr, EA_MODE_ABS, ctx.buf))
		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '______mmmrrr_s_bbbbb'
	// Used in 101101mmmrrr0s0bbbbb  O_BCHG     'dsp_ea_imm5 + bchg #n,X:ea / #n,Y:ea mmmrrr=ea, s=(X=0, Y=1), bbbbb=0-31'
	// Used in 101001mmmrrr0s0bbbbb  O_BCLR     'dsp_ea_imm5 + bclr #n,X:ea / #n,Y:ea mmmrrr=ea, s=(X=0, Y=1), bbbbb=0-31'
	// Used in 101001mmmrrr0s1bbbbb  O_BSET     'dsp_ea_imm5 + bset #n,X:ea / #n,Y:ea mmmrrr=ea, s=(X=0, Y=1), bbbbb=0-31'
	// Used in 101101mmmrrr0s1bbbbb  O_BTST     'dsp_ea_imm5 + btst #n,X:ea / #n,Y:ea mmmrrr=ea, s=(X=0, Y=1), bbbbb=0-31'
	// Used in 101101mmmrrr1s0bbbbb  O_JSCLR    'dsp_ea_imm5_abs16 + JSCLR #n,X:ea,xxxx / #n,Y:ea,xxxx n=bbbbb=0-31, ea=mmmrrr, xxxx=16bit extension, s=(X=0, Y=1)'
	// Used in 101001mmmrrr1s1bbbbb  O_JSET     'dsp_ea_imm5_abs16 + JSET #n,X:ea,xxxx / #n,Y:ea,xxxx n=bbbbb=0-31, ea=mmmrrr, xxxx=16bit extension, s=(X=0, Y=1)'
	// Used in 101101mmmrrr1s1bbbbb  O_JSSET    'dsp_ea_imm5_abs16 + JSSET #n,X:ea,xxxx / JSSET #n,Y:ea,xxxx n=bbbbb=0-31, ea=mmmrrr, xxxx=16bit extension, s=(X=0, Y=1)'
	// Used in 101001mmmrrr1s0bbbbb  O_JCLR     'dsp_ea_imm5_abs16 + JCLR #n,X:ea,xxxx / #n,Y:ea,xxxx n=bbbbb=0-31, ea=mmmrrr, xxxx=16bit extension, s=(X=0, Y=1)'
	static int decode_nonp_mmmrrrsbbbbb(nonp_context& ctx, Opcode opcode)
	{
		uint32_t bbbbb = (ctx.header >> 0) & 0x1f;
		set_imm(ctx.inst.operands[0], bbbbb);

		uint32_t s = (ctx.header >> 6) & 0x1;		// memory bit
		Memory mem = (s) ? MEM_Y : MEM_X;
		uint32_t mmm = (ctx.header >> 11) & 0x7;
		uint32_t rrr = (ctx.header >>  8) & 0x7;
		H56CHECK(decode_mmmrrr(ctx.inst.operands[1], mem, mmm, rrr, EA_MODE_ABS, ctx.buf))
		ctx.inst.opcode = opcode;
		if ((ctx.header >> 7) & 0x1)
			return decode_jcc_dest(ctx);
		return 0;
	}

	// Decoder for field type '______aaaaaa_s_bbbbb'
	// Used in 101100aaaaaa0s0bbbbb  O_BCHG     'dsp_ea_imm5 + bchg #n,X:aa / bchg #n,Y:aa'
	// Used in 101000aaaaaa0s0bbbbb  O_BCLR     'dsp_ea_imm5 + bclr #n,X:aa / bclr #n,Y:aa'
	// Used in 101000aaaaaa0s1bbbbb  O_BSET     'dsp_ea_imm5 + bset #n,X:aa / bset #n,Y:aa'
	// Used in 101100aaaaaa0s1bbbbb  O_BTST     'dsp_ea_imm5 + btst #n,X:aa / btst #n,Y:aa'
	// Used in 101100aaaaaa1s0bbbbb  O_JSCLR    'dsp_ea_imm5_abs16 + JSCLR #n,X:aa,xxxx / #n,Y:aa,xxxx n=bbbbb=0-31, aa=aaaaaa=short address, s=(X=0, Y=1)'
	// Used in 101000aaaaaa1s1bbbbb  O_JSET     'dsp_ea_imm5_abs16 + JSET #n,X:aa,xxxx / #n,Y:aa,xxxx n=bbbbb=0-31, aa=aaaaaa=short address, s=(X=0, Y=1)'
	// Used in 101100aaaaaa1s1bbbbb  O_JSSET    'dsp_ea_imm5_abs16 + JSSET #n,X:aa,xxxx / #n,Y:aa,xxxx n=bbbbb=0-31, aa=aaaaaa=short address, s=(X=0, Y=1)'
	// Used in 101000aaaaaa1s0bbbbb  O_JCLR     'dsp_ea_imm5_abs16 + JCLR #n,X:aa,xxxx / #n,Y:aa,xxxx n=bbbbb=0-31, aa=aaaaaa=short address, s=(X=0, Y=1)'
	static int decode_nonp_aaaaaasbbbbb(nonp_context& ctx, Opcode opcode)
	{
		uint32_t bbbbb = (ctx.header >> 0) & 0x1f;
		set_imm(ctx.inst.operands[0], bbbbb);

		uint32_t aaaaaa = (ctx.header >> 8) & 0x3f;
		uint32_t s = (ctx.header >> 6) & 0x1;		// memory bit
		ctx.inst.operands[1].memory = (s) ? MEM_Y : MEM_X;
		set_abs_short(ctx.inst.operands[1], aaaaaa);
		ctx.inst.opcode = opcode;
		if ((ctx.header >> 7) & 0x1)
			return decode_jcc_dest(ctx);
		return 0;
	}

	// Decoder for field type '______pppppp_s_bbbbb'
	// Used in 101110pppppp0s0bbbbb  O_BCHG     'dsp_ea_imm5 + bchg #n,X:pp / bchg #n,Y:pp'
	// Used in 101010pppppp0s0bbbbb  O_BCLR     'dsp_ea_imm5 + bclr #n,X:pp / bclr #n,Y:pp'
	// Used in 101010pppppp0s1bbbbb  O_BSET     'dsp_ea_imm5 + bset #n,X:pp / bset #n,Y:pp'
	// Used in 101110pppppp0s1bbbbb  O_BTST     'dsp_ea_imm5 + btst #n,X:pp / btst #n,Y:pp'
	// Used in 101110pppppp1s0bbbbb  O_JSCLR    'dsp_ea_imm5_abs16 + JSCLR #n,X:pp,xxxx / #n,Y:pp,xxxx n=bbbbb=0-31, pp=pppppp=short i/o address, s=(X=0, Y=1)'
	// Used in 101010pppppp1s1bbbbb  O_JSET     'dsp_ea_imm5_abs16 + JSET #n,X:pp,xxxx / #n,Y:pp,xxxx n=bbbbb=0-31, pp=pppppp=short i/o address, s=(X=0, Y=1)'
	// Used in 101110pppppp1s1bbbbb  O_JSSET    'dsp_ea_imm5_abs16 + JSSET #n,X:pp,xxxx / #n,Y:pp,xxxx n=bbbbb=0-31, pp=pppppp=short i/o address, s=(X=0, Y=1)'
	// Used in 101010pppppp1s0bbbbb  O_JCLR     'dsp_ea_imm5_abs16 + JCLR #n,X:pp,xxxx / #n,Y:pp,xxxx n=bbbbb=0-31, pp=pppppp=short i/o address, s=(X=0, Y=1)'
	static int decode_nonp_ppppppsbbbbb(nonp_context& ctx, Opcode opcode)
	{
		uint32_t bbbbb = (ctx.header >> 0) & 0x1f;
		set_imm(ctx.inst.operands[0], bbbbb);

		uint32_t pppppp = (ctx.header >> 8) & 0x3f;
		uint32_t s = (ctx.header >> 6) & 0x1;		// memory bit
		ctx.inst.operands[1].memory = (s) ? MEM_Y : MEM_X;
		set_io_short(ctx.inst.operands[1], pppppp);
		ctx.inst.opcode = opcode;
		if ((ctx.header >> 7) & 0x1)
			return decode_jcc_dest(ctx);
		return 0;
	}

	// Decoder for field type '__________dd___bbbbb'
	// Used in 1011110001dd010bbbbb  O_BCHG     'dsp_reg_imm5 + bchg #n,D DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 1010110001dd010bbbbb  O_BCLR     'dsp_reg_imm5 + bclr #n,D DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 1010110001dd011bbbbb  O_BSET     'dsp_reg_imm5 + bset #n,D DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 1011110001dd011bbbbb  O_BTST     'dsp_reg_imm5 + btst #n,D DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 1011110001dd000bbbbb  O_JSCLR    'dsp_reg_imm5_abs16 + JSCLR #n,S,xxxx DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 1010110001dd001bbbbb  O_JSET     'dsp_reg_imm5_abs16 + JSET #n,S,xxxx DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 1011110001dd001bbbbb  O_JSSET    'dsp_reg_imm5_abs16 + JSSET #n,S,xxxx DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 1010110001dd000bbbbb  O_JCLR     'dsp_reg_imm5_abs16 + JCLR #n,S,xxxx DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	static int decode_nonp_ddbbbbb(nonp_context& ctx, Opcode opcode)
	{
		uint32_t bbbbb = (ctx.header >> 0) & 0x1f;
		set_imm(ctx.inst.operands[0], bbbbb);
		uint32_t dd = (ctx.inst.header >> 8) & 0x3;
		H56CHECK(set_reg(ctx.inst.operands[1], registers_dd[dd]))
		ctx.inst.opcode = opcode;
		// NOTE this differs from the other JSET etc
		if (((ctx.header >> 6) & 0x1) == 0)
			return decode_jcc_dest(ctx);
		return 0;
	}

	// Decoder for field type '_________ddd___bbbbb'
	// Used in 101111001ddd010bbbbb  O_BCHG     'dsp_reg_imm5 + bchg #n,D DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111010ddd010bbbbb  O_BCHG     'dsp_reg_imm5 + bchg #n,D TTT See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111011ddd010bbbbb  O_BCHG     'dsp_reg_imm5 + bchg #n,D NNN See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111100ddd010bbbbb  O_BCHG     'dsp_reg_imm5 + bchg #n,D FFF See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111111ddd010bbbbb  O_BCHG     'dsp_reg_imm5 bchg #n,D GGG See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011001ddd010bbbbb  O_BCLR     'dsp_reg_imm5 + bclr #n,D DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011010ddd010bbbbb  O_BCLR     'dsp_reg_imm5 + bclr #n,D TTT See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011011ddd010bbbbb  O_BCLR     'dsp_reg_imm5 + bclr #n,D NNN See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011100ddd010bbbbb  O_BCLR     'dsp_reg_imm5 + bclr #n,D FFF See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011111ddd010bbbbb  O_BCLR     'dsp_reg_imm5 bclr #n,D GGG See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011001ddd011bbbbb  O_BSET     'dsp_reg_imm5 + bset #n,D DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011010ddd011bbbbb  O_BSET     'dsp_reg_imm5 + bset #n,D TTT See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011011ddd011bbbbb  O_BSET     'dsp_reg_imm5 + bset #n,D NNN See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011100ddd011bbbbb  O_BSET     'dsp_reg_imm5 + bset #n,D FFF See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011111ddd011bbbbb  O_BSET     'dsp_reg_imm5 bset #n,D GGG See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111001ddd011bbbbb  O_BTST     'dsp_reg_imm5 + btst #n,D DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111010ddd011bbbbb  O_BTST     'dsp_reg_imm5 + btst #n,D TTT See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111011ddd011bbbbb  O_BTST     'dsp_reg_imm5 + btst #n,D NNN See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111100ddd011bbbbb  O_BTST     'dsp_reg_imm5 + btst #n,D FFF See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111111ddd011bbbbb  O_BTST     'dsp_reg_imm5 btst #n,D GGG See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111001ddd000bbbbb  O_JSCLR    'dsp_reg_imm5_abs16 + JSCLR #n,S,xxxx DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111010ddd000bbbbb  O_JSCLR    'dsp_reg_imm5_abs16 + JSCLR #n,S,xxxx TTT See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111011ddd000bbbbb  O_JSCLR    'dsp_reg_imm5_abs16 + JSCLR #n,S,xxxx NNN See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111100ddd000bbbbb  O_JSCLR    'dsp_reg_imm5_abs16 + JSCLR #n,S,xxxx FFF See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111101ddd000bbbbb  O_JSCLR    'dsp_reg_imm5_abs16 JSCLR #n,S,xxxx GGG See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011001ddd001bbbbb  O_JSET     'dsp_reg_imm5_abs16 + JSET #n,S,xxxx DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011010ddd001bbbbb  O_JSET     'dsp_reg_imm5_abs16 + JSET #n,S,xxxx TTT See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011011ddd001bbbbb  O_JSET     'dsp_reg_imm5_abs16 + JSET #n,S,xxxx NNN See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011100ddd001bbbbb  O_JSET     'dsp_reg_imm5_abs16 + JSET #n,S,xxxx FFF See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011101ddd001bbbbb  O_JSET     'dsp_reg_imm5_abs16 JSET #n,S,xxxx GGG See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111001ddd001bbbbb  O_JSSET    'dsp_reg_imm5_abs16 + JSSET #n,S,xxxx DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111010ddd001bbbbb  O_JSSET    'dsp_reg_imm5_abs16 + JSSET #n,S,xxxx TTT See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111011ddd001bbbbb  O_JSSET    'dsp_reg_imm5_abs16 + JSSET #n,S,xxxx NNN See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111100ddd001bbbbb  O_JSSET    'dsp_reg_imm5_abs16 + JSSET #n,S,xxxx FFF See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101111101ddd001bbbbb  O_JSSET    'dsp_reg_imm5_abs16 JSSET #n,S,xxxx GGG See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011001ddd000bbbbb  O_JCLR     'dsp_reg_imm5_abs16 + JCLR #n,S,xxxx DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011010ddd000bbbbb  O_JCLR     'dsp_reg_imm5_abs16 + JCLR #n,S,xxxx TTT See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011011ddd000bbbbb  O_JCLR     'dsp_reg_imm5_abs16 + JCLR #n,S,xxxx NNN See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011100ddd000bbbbb  O_JCLR     'dsp_reg_imm5_abs16 + JCLR #n,S,xxxx FFF See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 101011101ddd000bbbbb  O_JCLR     'dsp_reg_imm5_abs16 JCLR #n,S,xxxx GGG See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	static int decode_nonp_dddbbbbb(nonp_context& ctx, Opcode opcode)
	{
		uint32_t bbbbb = (ctx.header >> 0) & 0x1f;
		set_imm(ctx.inst.operands[0], bbbbb);

		// IMPORTANT NOTE
		// This is misleading since the "ddd" actually matches 6 bits (including the 3 above)
		uint32_t dddddd = (ctx.inst.header >> 8) & 0x3f;
		H56CHECK(set_reg(ctx.inst.operands[1], registers_triple_bit[dddddd]))

		ctx.inst.opcode = opcode;
		// NOTE this differs from the other JSET etc
		if (((ctx.header >> 6) & 0x1) == 0)
			return decode_jcc_dest(ctx);
		return 0;
	}

	// Decoder for field type '______mmmrrr_s______'
	// Used in 011001mmmrrr0s000000  O_DO       'dsp_ea_abs16 + DO X:ea,expr / DO Y:ea,expr mmmrrr=ea, s=(X=0, Y=1), expr=16bit in extension word'
	// Used in 011001mmmrrr0s100000  O_REP      'dsp_ea + rep x:ea / y:ea'
	static int decode_nonp_mmmrrrs(nonp_context& ctx, Opcode opcode)
	{
		uint32_t mmm = (ctx.header >> 11) & 0x7;
		uint32_t rrr = (ctx.header >>  8) & 0x7;
		uint32_t s = (ctx.header >> 6) & 0x1;		// memory bit
		Memory mem = (s) ? MEM_Y : MEM_X;
		H56CHECK(decode_mmmrrr(ctx.inst.operands[0], mem, mmm, rrr, EA_MODE_REGS, ctx.buf))
		ctx.inst.opcode = opcode;
		return decode_rep_loop_address(ctx);
	}

	// Decoder for field type '______aaaaaa_s______'
	// Used in 011000aaaaaa0s000000  O_DO       'dsp_ea_abs16 + DO X:aa,expr / DO Y:aa,expr aaaaaa=aa, s=(X=0, Y=1), expr=16bit in extension word'
	// Used in 011000aaaaaa0s100000  O_REP      'dsp_ea + rep x:aa / y:aa'
	static int decode_nonp_aaaaaas(nonp_context& ctx, Opcode opcode)
	{
		// Short abs address
		uint32_t aaaaaa = (ctx.header >> 8) & 0x3f;
		uint32_t s = (ctx.header >> 6) & 0x1;		// memory bit
		ctx.inst.operands[0].memory = (s) ? MEM_Y : MEM_X;
		set_abs_short(ctx.inst.operands[0], aaaaaa);

		ctx.inst.opcode = opcode;
		return decode_rep_loop_address(ctx);
	}

	// Decoder for field type '____iiiiiiii____hhhh'
	// Used in 0110iiiiiiii1000hhhh  O_DO       'dsp_imm12_abs16 + DO #xxx,expr hhhhiiiiiiii=12bit immediate, expr=16bit in extension word'
	// Used in 0110iiiiiiii1010hhhh  O_REP      'dsp_imm12 + rep #xx'
	static int decode_nonp_iiiiiiiihhhh(nonp_context& ctx, Opcode opcode)
	{
		uint32_t hhhhiiiiiiii = (ctx.inst.header & 0xf) << 8;
		hhhhiiiiiiii |= (ctx.inst.header >> 8) & 0xff;
		set_imm(ctx.inst.operands[0], hhhhiiiiiiii);

		ctx.inst.opcode = opcode;
		return decode_rep_loop_address(ctx);
	}

	// Decoder for field type '_________ddd________'
	// Used in 011011000ddd00000000  O_DO       'dsp_alu24_abs16 + DO S,expr x0, x1, y0, y1'
	// Used in 011011001ddd00000000  O_DO       'dsp_reg_abs16 + DO S,expr DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 011011010ddd00000000  O_DO       'dsp_reg_abs16 + DO S,expr TTT See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 011011011ddd00000000  O_DO       'dsp_reg_abs16 + DO S,expr NNN See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 011011100ddd00000000  O_DO       'dsp_reg_abs16 + DO S,expr FFF See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 011011111ddd00000000  O_DO       'dsp_reg_abs16 DO S,expr GGG See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 011011000ddd00100000  O_REP      'dsp_alu24 + rep S,expr x0, x1, y0, y1'
	// Used in 011011001ddd00100000  O_REP      'dsp_reg + rep S DDD See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 011011010ddd00100000  O_REP      'dsp_reg + rep S TTT See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 011011011ddd00100000  O_REP      'dsp_reg + rep S NNN See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 011011100ddd00100000  O_REP      'dsp_reg + rep S FFF See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	// Used in 011011111ddd00100000  O_REP      'dsp_reg rep S GGG See A.9 Instruction Encoding and Table A-18 for specific register encodings.'
	static int decode_nonp_ddd(nonp_context& ctx, Opcode opcode)
	{
		// IMPORTANT NOTE
		// This is misleading since the "ddd" actually matches 6 bits (including the 3 above)
		uint32_t dddddd = (ctx.inst.header >> 8) & 0x3f;
		H56CHECK(set_reg(ctx.inst.operands[0], registers_triple_bit[dddddd]))
		ctx.inst.opcode = opcode;
		return decode_rep_loop_address(ctx);
	}

	// Decoder for field type '_______mmrrr____dddd'
	// Used in 0100010mmrrr0001dddd  O_LUA      'dsp_ea_lua mmrrr=ea (subset), dddd=(bit 3=(0=Rn, 1=Nn), bits 2-0=0-7)'
	static int decode_nonp_mmrrrdddd(nonp_context& ctx, Opcode opcode)
	{
		uint32_t mmm = (ctx.header >> 11) & 0x7;
		uint32_t rrr = (ctx.header >>  8) & 0x7;
		Memory mem = MEM_NONE;
		// NOTE: as well as using MODE_LUA the nonp_tables protect us since "0" is specified
		// in the top bit of the mmm area.
		H56CHECK(decode_mmmrrr(ctx.inst.operands[0], mem, mmm, rrr, EA_MODE_LUA, ctx.buf))

		uint32_t is_n = (ctx.header >> 3) & 0x1;
		uint32_t ddd = (ctx.header >> 0) & 0x7;
		H56CHECK(set_reg(ctx.inst.operands[1], is_n ? make_n(ddd) : make_r(ddd)))
		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '_________rrr____d___'
	// Used in 000111011rrr0001d101  O_NORM     'dsp_ab_rn norm Rn,D D=(a=0, b=1)'
	static int decode_nonp_rrrd(nonp_context& ctx, Opcode opcode)
	{
		uint32_t rrr = (ctx.header >> 8) & 0x7;
		uint32_t d = (ctx.header >> 3) & 0x1;
		H56CHECK(set_reg(ctx.inst.operands[0], make_r(rrr)))
		H56CHECK(set_reg(ctx.inst.operands[1], registers_a_or_b[d]))
		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '____iiiiiiii___ddddd'
	// Used in 0101iiiiiiii101ddddd  O_MOVEC    'dsp_immmovec + move(c) #xx,d1'
	static int decode_nonp_iiiiiiiiddddd(nonp_context& ctx, Opcode opcode)
	{
		uint32_t ddddd = (ctx.header >> 0) & 0x1f;
		uint32_t iiiiiiii = (ctx.header >> 8) & 0xff;
		// Is this signed or not? It sort of depends on the register!
		set_imm(ctx.inst.operands[0], iiiiiiii);
		H56CHECK(set_reg(ctx.inst.operands[1], registers_movec[ddddd]))
		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '______mmmrrr_s_ddddd'
	// Used in 010111mmmrrr0s1ddddd  O_MOVEC    'dsp_movec_ea + move(c) x:ea,d1 / y:ea,d1'
	// Used in 010101mmmrrr0s1ddddd  O_MOVEC    'dsp_movec_ea + move(c) s1,x:ea / s1,y:ea'
	static int decode_nonp_mmmrrrsddddd(nonp_context& ctx, Opcode opcode)
	{
		// Choose operand ordering
		operand* opA, *opB;
		uint32_t w = (ctx.inst.header >> 15) & 0x1;
		allocate_operands(ctx.inst, w, opA, opB);

		uint32_t mmm = (ctx.header >> 11) & 0x7;
		uint32_t rrr = (ctx.header >>  8) & 0x7;
		uint32_t s = (ctx.header >> 6) & 0x1;		// memory bit
		Memory mem = (s) ? MEM_Y : MEM_X;

		// NOTE: this isn't really important since the tables mean
		// that a different MOVEC decoder is used for immediate
		EA_MODE mode = w ? EA_MODE_ALL : EA_MODE_ABS;	// here w==0 means "reading from reg, and writing to memory"
		H56CHECK(decode_mmmrrr(*opA, mem, mmm, rrr, mode, ctx.buf))

		uint32_t ddddd = (ctx.header >> 0) & 0x1f;
		H56CHECK(set_reg(*opB, registers_movec[ddddd]))

		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '_______________ddddd'
	// Used in 010111110100001ddddd  O_MOVEC    'dsp_movec_ea + move(c) #xxxx,d1'
	static int decode_nonp_ddddd(nonp_context& ctx, Opcode opcode)
	{
		uint32_t imm_val;
		H56CHECK(ctx.buf.read_word(imm_val))

		set_imm(ctx.inst.operands[0], imm_val);
		uint32_t ddddd = (ctx.header >> 0) & 0x1f;
		H56CHECK(set_reg(ctx.inst.operands[1], registers_movec[ddddd]))
		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '______aaaaaa_s_ddddd'
	// Used in 010110aaaaaa0s1ddddd  O_MOVEC    'dsp_movec_aa + move(c) x:aa,d1 / y:aa,d1'
	// Used in 010100aaaaaa0s1ddddd  O_MOVEC    'dsp_movec_aa + move(c) s1,x:aa / s1,y:aa'
	static int decode_nonp_aaaaaasddddd(nonp_context& ctx, Opcode opcode)
	{
		// Choose operand ordering
		operand* opA, *opB;
		uint32_t w = (ctx.inst.header >> 15) & 0x1;
		allocate_operands(ctx.inst, w, opA, opB);

		uint32_t aaaaaa = (ctx.header >> 8) & 0x3f;
		uint32_t s = (ctx.header >> 6) & 0x1;		// memory bit
		opA->memory = (s) ? MEM_Y : MEM_X;
		set_abs_short(*opA, aaaaaa);

		uint32_t ddddd = (ctx.header >> 0) & 0x1f;
		H56CHECK(set_reg(*opB, registers_movec[ddddd]))

		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '_________eee___ddddd'
	// Used in 010001000eee101ddddd  O_MOVEC    'dsp_movec_reg + move(c) s1,d2'
	// Used in 010001001eee101ddddd  O_MOVEC    'dsp_movec_reg + move(c) s1,d2'
	// Used in 010001010eee101ddddd  O_MOVEC    'dsp_movec_reg + move(c) s1,d2'
	// Used in 010001011eee101ddddd  O_MOVEC    'dsp_movec_reg + move(c) s1,d2'
	// Used in 010001100eee101ddddd  O_MOVEC    'dsp_movec_reg + move(c) s1,d2'
	// Used in 010001111eee101ddddd  O_MOVEC    'dsp_movec_reg + move(c) s1,d2'
	// Used in 010011000eee101ddddd  O_MOVEC    'dsp_movec_reg + move(c) s2,d1'
	// Used in 010011001eee101ddddd  O_MOVEC    'dsp_movec_reg + move(c) s2,d1'
	// Used in 010011010eee101ddddd  O_MOVEC    'dsp_movec_reg + move(c) s2,d1'
	// Used in 010011011eee101ddddd  O_MOVEC    'dsp_movec_reg + move(c) s2,d1'
	// Used in 010011100eee101ddddd  O_MOVEC    'dsp_movec_reg + move(c) s2,d1'
	// Used in 010011111eee101ddddd  O_MOVEC    'dsp_movec_reg move(c) s2,d1'
	static int decode_nonp_eeeddddd(nonp_context& ctx, Opcode opcode)
	{
		// Choose operand ordering
		operand* opA, *opB;
		uint32_t w = (ctx.inst.header >> 15) & 0x1;
		allocate_operands(ctx.inst, w, opA, opB);

		uint32_t eeeeee = (ctx.inst.header >> 8) & 0x3f;
		H56CHECK(set_reg(*opA, registers_triple_bit[eeeeee]))

		uint32_t ddddd = (ctx.header >> 0) & 0x1f;
		H56CHECK(set_reg(*opB, registers_movec[ddddd]))

		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '______mmmrrr_____ddd'
	// Used in 011101mmmrrr10000ddd  O_MOVEM    'dsp_movem_ea + move(m) s,p:ea / p:ea,d'
	// Used in 011101mmmrrr10001ddd  O_MOVEM    'dsp_movem_ea + move(m) s,p:ea / p:ea,d'
	// Used in 011101mmmrrr10010ddd  O_MOVEM    'dsp_movem_ea + move(m) s,p:ea / p:ea,d'
	// Used in 011101mmmrrr10011ddd  O_MOVEM    'dsp_movem_ea + move(m) s,p:ea / p:ea,d'
	// Used in 011101mmmrrr10100ddd  O_MOVEM    'dsp_movem_ea + move(m) s,p:ea / p:ea,d'
	// Used in 011101mmmrrr10111ddd  O_MOVEM    'dsp_movem_ea + move(m) s,p:ea / p:ea,d'
	// Used in 011111mmmrrr10000ddd  O_MOVEM    'dsp_movem_ea + move(m) s,p:ea / p:ea,d'
	// Used in 011111mmmrrr10001ddd  O_MOVEM    'dsp_movem_ea + move(m) s,p:ea / p:ea,d'
	// Used in 011111mmmrrr10010ddd  O_MOVEM    'dsp_movem_ea + move(m) s,p:ea / p:ea,d'
	// Used in 011111mmmrrr10011ddd  O_MOVEM    'dsp_movem_ea + move(m) s,p:ea / p:ea,d'
	// Used in 011111mmmrrr10100ddd  O_MOVEM    'dsp_movem_ea + move(m) s,p:ea / p:ea,d'
	// Used in 011111mmmrrr10111ddd  O_MOVEM    'dsp_movem_ea + move(m) s,p:ea / p:ea,d'
	static int decode_nonp_mmmrrrddd(nonp_context& ctx, Opcode opcode)
	{
		// Choose operand ordering
		operand* opA, *opB;
		uint32_t w = (ctx.inst.header >> 15) & 0x1;
		allocate_operands(ctx.inst, w, opA, opB);

		uint32_t mmm = (ctx.header >> 11) & 0x7;
		uint32_t rrr = (ctx.header >>  8) & 0x7;
		Memory mem = MEM_P;
		H56CHECK(decode_mmmrrr(*opA, mem, mmm, rrr, EA_MODE_ABS, ctx.buf))

		uint32_t dddddd = (ctx.header >> 0) & 0x3f;
		H56CHECK(set_reg(*opB, registers_triple_bit[dddddd]))

		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '______aaaaaa_____ddd'
	// Used in 011100aaaaaa00000ddd  O_MOVEM    'dsp_movem_aa + move(m) s,p:aa / p:aa,d'
	// Used in 011100aaaaaa00001ddd  O_MOVEM    'dsp_movem_aa + move(m) s,p:aa / p:aa,d'
	// Used in 011100aaaaaa00010ddd  O_MOVEM    'dsp_movem_aa + move(m) s,p:aa / p:aa,d'
	// Used in 011100aaaaaa00011ddd  O_MOVEM    'dsp_movem_aa + move(m) s,p:aa / p:aa,d'
	// Used in 011100aaaaaa00100ddd  O_MOVEM    'dsp_movem_aa + move(m) s,p:aa / p:aa,d'
	// Used in 011100aaaaaa00111ddd  O_MOVEM    'dsp_movem_aa + move(m) s,p:aa / p:aa,d'
	// Used in 011110aaaaaa00000ddd  O_MOVEM    'dsp_movem_aa + move(m) s,p:aa / p:aa,d'
	// Used in 011110aaaaaa00001ddd  O_MOVEM    'dsp_movem_aa + move(m) s,p:aa / p:aa,d'
	// Used in 011110aaaaaa00010ddd  O_MOVEM    'dsp_movem_aa + move(m) s,p:aa / p:aa,d'
	// Used in 011110aaaaaa00011ddd  O_MOVEM    'dsp_movem_aa + move(m) s,p:aa / p:aa,d'
	// Used in 011110aaaaaa00100ddd  O_MOVEM    'dsp_movem_aa + move(m) s,p:aa / p:aa,d'
	// Used in 011110aaaaaa00111ddd  O_MOVEM    'dsp_movem_aa move(m) s,p:aa / p:aa,d'
	static int decode_nonp_aaaaaaddd(nonp_context& ctx, Opcode opcode)
	{
		// Choose operand ordering
		operand* opA, *opB;
		uint32_t w = (ctx.inst.header >> 15) & 0x1;
		allocate_operands(ctx.inst, w, opA, opB);

		uint32_t aaaaaa = (ctx.header >> 8) & 0x3f;
		opA->memory = MEM_P;
		set_abs_short(*opA, aaaaaa);

		uint32_t dddddd = (ctx.header >> 0) & 0x3f;
		H56CHECK(set_reg(*opB, registers_movem[dddddd]))

		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '___sw_mmmrrr_spppppp'
	// Used in 100sw1mmmrrr1spppppp  O_MOVEP    'dsp_movep_ea MOVEP XY:ea,XY:pp'
	static int decode_nonp_swmmmrrrspppppp(nonp_context& ctx, Opcode opcode)
	{
		// Choose operand ordering
		operand* opA, *opB;
		uint32_t w = (ctx.inst.header >> 15) & 0x1;
		allocate_operands(ctx.inst, w, opA, opB);

		uint32_t mmm = (ctx.header >> 11) & 0x7;
		uint32_t rrr = (ctx.header >>  8) & 0x7;

		Memory mem = (ctx.header >> 6) & 0x1 ? MEM_Y : MEM_X;
		EA_MODE mode = w ? EA_MODE_ALL : EA_MODE_ABS;	// here w==0 means "reading from reg, and writing to memory"
		H56CHECK(decode_mmmrrr(*opA, mem, mmm, rrr, mode, ctx.buf))

		uint32_t pppppp = (ctx.header >> 0) & 0x3f;
		set_io_short(*opB, pppppp);
		opB->memory = (ctx.header >> 16) & 0x1 ? MEM_Y : MEM_X;

		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '___sw_mmmrrr__pppppp'
	// Used in 100sw1mmmrrr01pppppp  O_MOVEP    'dsp_movep_ea MOVEP P:ea,XY:pp'
	static int decode_nonp_swmmmrrrpppppp(nonp_context& ctx, Opcode opcode)
	{
		// Choose operand ordering
		operand* opA, *opB;
		uint32_t w = (ctx.inst.header >> 15) & 0x1;
		allocate_operands(ctx.inst, w, opA, opB);

		// Bit 7 decides whether this is EA or AA
		uint32_t mmm = (ctx.header >> 11) & 0x7;
		uint32_t rrr = (ctx.header >>  8) & 0x7;
		H56CHECK(decode_mmmrrr(*opA, MEM_P, mmm, rrr, EA_MODE_ABS, ctx.buf))

		uint32_t pppppp = (ctx.header >> 0) & 0x3f;
		set_io_short(*opB, pppppp);
		opB->memory = (ctx.header >> 16) & 0x1 ? MEM_Y : MEM_X;

		ctx.inst.opcode = opcode;
		return 0;
	}

	// Decoder for field type '___sw_dddddd__pppppp'
	// Used in 100sw1dddddd00pppppp  O_MOVEP    'dsp_movep_ea MOVEP REG,XY:pp'
	static int decode_nonp_swddddddpppppp(nonp_context& ctx, Opcode opcode)
	{
		// Choose operand ordering
		operand* opA, *opB;
		uint32_t w = (ctx.inst.header >> 15) & 0x1;
		allocate_operands(ctx.inst, w, opA, opB);

		uint32_t dddddd = (ctx.inst.header >> 8) & 0x3f;
		H56CHECK(set_reg(*opA, registers_triple_bit[dddddd]))


		uint32_t pppppp = (ctx.header >> 0) & 0x3f;
		set_io_short(*opB, pppppp);
		opB->memory = (ctx.header >> 16) & 0x1 ? MEM_Y : MEM_X;

		ctx.inst.opcode = opcode;
		return 0;
	}


	// Decoder for field type '___P__PPPPPPPPPPPPPP'
	static int decode_nonp_PPPPPPPPPPPPPPP(nonp_context& ctx, Opcode /*opcode*/)
	{
		// This is to handle parallel moves which end up with 0000
		// in the top 4 bits. These are simply passed through to the
		// parallel move decoder function.
		return decode_pm(ctx.inst, ctx.header, ctx.settings, ctx.buf);
	}

	// ========================================================================
	//	OPCODE TABLE FOR NON-PARALLEL MOVE INSTRUCTIONS
	// ========================================================================

#include "nonp_tables.i"

	// Entry point for the non-parallel-move opcodes, which don't have
	// "0000" as the top 4 bits.
	// There are some parallel move opcodes in here, but they eventually get
	// passed through to the decode_pm() function.
	int decode_non_pm(instruction& inst, uint32_t header, const decode_settings& settings, buffer_reader& buf)
	{
		// Use bits 14,15,16,17,18,19 for the table entry
		uint8_t base_type = (inst.header >> 14) & 0x3f;
		nonp_context ctx(inst, buf, settings);
		ctx.header = header;
		inst.word_count = 1;		// default values
		dsp_decoder func = g_nonp_tables[base_type];
		int ret = func(ctx);
		return ret;
	}

	int decode(instruction& inst, buffer_reader& buf, const decode_settings& settings)
	{
		inst.reset();
		buffer_reader buf_copy = buf;

		H56CHECK(buf_copy.read_word(inst.header))

		// Decide on parallel-move vs non-parallel-move decode.
		// For non-parallel moves (except "B,X:(R1)+	X0,B" type)
		// the top 4 bits are zero.
		uint32_t top_bits = (inst.header >> 20) & 0xffff;
		int ret = 0;
		if (top_bits == 0x0)
			// Instruction without parallel move
			ret = decode_non_pm(inst, inst.header, settings, buf_copy);
		else
			ret = decode_pm(inst, inst.header, settings, buf_copy);

		if (ret == 0)
		{
			inst.word_count = buf_copy.get_pos() - buf.get_pos();
			return ret;
		}

		// Decode failure.
		inst.opcode = Opcode::INVALID;
		inst.word_count = 1;
		set_abs(inst.operands[0], inst.header);
		buf.advance(1U);
		return ret;
	}
}
