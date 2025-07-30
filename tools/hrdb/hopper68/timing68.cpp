#include "timing68.h"
#include "instruction68.h"

namespace hop68
{
struct time_entry
{
	Opcode		op;
	Suffix		suffix;
	OpType		type0;
	OpType		type1;
	uint8_t		time_min;
	uint8_t		time_max;
	uint8_t		flags;
};

#define TIMING(op, suf, type0, type1, time, flags)	{	Opcode::op, Suffix::suf, OpType::type0, OpType::type1, time, time, flags }

static const time_entry g_timingEntry[] =
{
#include "timing_table.i"

	{ Opcode::COUNT, Suffix::NONE, OpType::INVALID, OpType::INVALID, 0, 0, 0}
};

// Special case: move instructions are effectively a 2d sum
bool check_standard_move(const instruction& inst, timing& result)
{
	if (inst.opcode != Opcode::MOVE)
		return 1;

	// Calc is 4 + (op0 time) + (op1 time)
	uint16_t cycles = 4;
	if (inst.suffix == Suffix::BYTE || inst.suffix == Suffix::WORD)
	{
		switch (inst.op0.type)
		{
			case OpType::D_DIRECT:			cycles += 0; break;
			case OpType::A_DIRECT:			cycles += 0; break;
			case OpType::INDIRECT:			cycles += 4; break;
			case OpType::INDIRECT_POSTINC:	cycles += 4; break;
			case OpType::INDIRECT_PREDEC:	cycles += 6; break;
			case OpType::INDIRECT_DISP:		cycles += 8; break;
			case OpType::INDIRECT_INDEX:	cycles += 10; break;
			case OpType::ABSOLUTE_WORD:		cycles += 8; break;
			case OpType::ABSOLUTE_LONG:		cycles += 12; break;
			case OpType::PC_DISP:			cycles += 8; break;
			case OpType::PC_DISP_INDEX:		cycles += 10; break;
			case OpType::IMMEDIATE:			cycles += 4; break;
			default: return 1;
		}
		switch (inst.op1.type)
		{
			case OpType::D_DIRECT:			cycles += 0; break;
			case OpType::A_DIRECT:			cycles += 0; break;
			case OpType::INDIRECT:			cycles += 4; break;
			case OpType::INDIRECT_POSTINC:	cycles += 4; break;
			case OpType::INDIRECT_PREDEC:	cycles += 4; break;
			case OpType::INDIRECT_DISP:		cycles += 8; break;
			case OpType::INDIRECT_INDEX:	cycles += 10; break;
			case OpType::ABSOLUTE_WORD:		cycles += 8; break;
			case OpType::ABSOLUTE_LONG:		cycles += 12; break;
			default: return 1;
		}
		result.min = result.max = cycles;
		return 0;
	}
	else if (inst.suffix == Suffix::LONG)
	{
		switch (inst.op0.type)
		{
			case OpType::D_DIRECT:			cycles += 0; break;
			case OpType::A_DIRECT:			cycles += 0; break;
			case OpType::INDIRECT:			cycles += 8; break;
			case OpType::INDIRECT_POSTINC:	cycles += 8; break;
			case OpType::INDIRECT_PREDEC:	cycles += 10; break;
			case OpType::INDIRECT_DISP:		cycles += 12; break;
			case OpType::INDIRECT_INDEX:	cycles += 12; break;
			case OpType::ABSOLUTE_WORD:		cycles += 14; break;
			case OpType::ABSOLUTE_LONG:		cycles += 12; break;
			case OpType::PC_DISP:			cycles += 16; break;
			case OpType::PC_DISP_INDEX:		cycles += 12; break;
			case OpType::IMMEDIATE:			cycles += 14; break;
			default: return 1;
		}
		switch (inst.op1.type)
		{
			case OpType::D_DIRECT:			cycles += 0; break;
			case OpType::A_DIRECT:			cycles += 0; break;
			case OpType::INDIRECT:			cycles += 8; break;
			case OpType::INDIRECT_POSTINC:	cycles += 8; break;
			case OpType::INDIRECT_PREDEC:	cycles += 10; break;
			case OpType::INDIRECT_DISP:		cycles += 12; break;
			case OpType::INDIRECT_INDEX:	cycles += 14; break;
			case OpType::ABSOLUTE_WORD:		cycles += 12; break;
			case OpType::ABSOLUTE_LONG:		cycles += 16; break;
			default: return 1;
		}
		result.min = result.max = cycles;
		return 0;
	}
	return 1;
}

int calc_timing(const instruction& inst, timing& result)
{
	// Special case: move instruction
	result.min = result.max = 0;
	result.flags = 0;
	if (check_standard_move(inst, result) == 0)
		return 0;

	for (const time_entry* curr_entry = g_timingEntry;
		curr_entry->op != Opcode::COUNT;
		++curr_entry)
	{
		if (curr_entry->op != inst.opcode)
			continue;
		if (inst.suffix != Suffix::NONE &&
			curr_entry->suffix != Suffix::NONE &&
			 curr_entry->suffix != inst.suffix)
			continue;
		if ( curr_entry->type0 != OpType::INVALID &&
			 inst.op0.type     != OpType::INVALID &&
			 curr_entry->type0 != inst.op0.type)
			continue;
		if (curr_entry->type1 != OpType::INVALID &&
			 inst.op1.type     != OpType::INVALID &&
			 curr_entry->type1 != inst.op1.type)
			continue;

		result.min = curr_entry->time_min;
		result.max = curr_entry->time_max;
		result.flags = curr_entry->flags;
		return 0;
	}
    return 1;
}
}
