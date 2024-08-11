#ifndef DECODE_H
#define DECODE_H

namespace hopper68
{
class buffer_reader;
struct instruction;

enum
{
	CPU_TYPE_68000,
	CPU_TYPE_68010,
	CPU_TYPE_68020,
	CPU_TYPE_68030
};

struct decode_settings
{
	int cpu_type;
};

// decode a single instruction. The instruction's opcode will be set to "invalid", with a byte
// size of 2, if no match was found.
extern void decode(instruction& inst, buffer_reader& buffer, const decode_settings& dsettings);

}
#endif
