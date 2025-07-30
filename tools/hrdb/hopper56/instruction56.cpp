#include "instruction56.h"
#include "opcode_strings.i"

namespace hop56
{
	// ----------------------------------------------------------------------------
	const char* g_register_names[Reg::REG_COUNT] =
	{
		"?",

		"a",
		"b",
		"x",
		"y",

		"a0", "a1", "a2",
		"b0", "b1", "b2",
		"x0", "x1",
		"y0", "y1",

		"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
		"n0", "n1", "n2", "n3", "n4", "n5", "n6", "n7",
		"m0", "m1", "m2", "m3", "m4", "m5", "m6", "m7",

		// register pairs/parts for movel types
		"a10", "b10", "ab", "ba",

		"mr", "ccr", "omr",
		"sr", "sp", "ssh", "ssl", "la", "lc"
	};

	// ----------------------------------------------------------------------------
	const char* g_memory_names[Memory::MEM_COUNT] =
	{
		"", "x:", "y:", "p:", "l:"
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
	const char* get_register_string(Reg reg)
	{
		if (reg < ARRAY_SIZE(g_register_names))
			return g_register_names[reg];
		return "?";
	}

	// ----------------------------------------------------------------------------
	const char* get_memory_string(Memory mem)
	{
		if (mem < ARRAY_SIZE(g_memory_names))
			return g_memory_names[mem];
		return "?";
	}

	// ----------------------------------------------------------------------------
	instruction::instruction() :
		header(0U),
		word_count(0U),
		opcode(Opcode::INVALID)
	{
		reset();
	}

	// ----------------------------------------------------------------------------
	void instruction::reset()
	{
		// This effectively sets the instruction to a "dc.w" statement
		header = 0U;
		word_count = 1U;
		opcode = Opcode::INVALID;
		neg_operands = 0;
		operands[0].reset();
		operands[1].reset();
		operands[2].reset();

		operands2[0].reset();
		operands2[1].reset();

		pmoves[0].reset();
	   	pmoves[1].reset();
	}
}

