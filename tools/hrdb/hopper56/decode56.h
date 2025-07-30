#ifndef HOPPER56_DECODE_H
#define HOPPER56_DECODE_H

namespace hop56
{
	struct instruction;
	class buffer_reader;

	// Setup for any potential decoder preferences e.g. processor type.
	struct decode_settings
	{
	};

	// Tokenise a single 56000 instruction from a buffer reader.
	// 'inst' will contain the instruction opcode and operands, including "DC" for invalid instructions.
	// 'buf' points to instruction memory with a bounded size. The buffer read pointer
	//       will be moved to the end of this instruction.
	int decode(instruction& inst, buffer_reader& buf, const decode_settings& settings);
}

#endif // HOPPER56_DECODE_H

