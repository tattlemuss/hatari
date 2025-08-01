#ifndef DISASSEMBLER56_H
#define DISASSEMBLER56_H
#include "stdint.h"
#include <QVector>

#include "hopper56/decode56.h"
#include "hopper56/instruction56.h"

class QTextStream;
class DspRegisters;

namespace hop56
{
    class buffer_reader;
}

class Disassembler56
{
public:

    // Describes a DSP address.
    struct addr_t
    {
        hop56::Memory mem;
        uint32_t	  addr;

        bool operator<(const addr_t& other) const
        {
            if (mem != other.mem)
                return mem < other.mem;
            return addr < other.addr;
        }
    };

    struct line
    {
        uint32_t             address;
        hop56::instruction   inst;
        uint8_t              mem[3*6];          // Copy of instruction memory

        // Calc end address of the instruction (in DSP P: memory words)
        uint32_t    GetEnd() const
        {
            return address + inst.word_count;
        }
    };

    // ----------------------------------------------------------------------------
    //	HIGHER-LEVEL DISASSEMBLY CREATION
    // ----------------------------------------------------------------------------
    // Storage for an attempt at tokenising the memory
    class disassembly
    {
    public:
        QVector<line>    lines;
    };

    // Try to decode a single instruction
    static void decode_inst(hop56::buffer_reader& buf, hop56::instruction& inst, const hop56::decode_settings& settings);

    // Decode a block of instructions
    static int decode_buf(hop56::buffer_reader& buf, disassembly& disasm, const hop56::decode_settings& settings, uint32_t address, int32_t maxLines);

    // Format a single instruction and its arguments
    static int print_inst(const hop56::instruction& inst, QTextStream& ref);

    // Format a single instruction and its arguments
    static int print_terse(const hop56::instruction& inst, QTextStream& ref);

    static bool calc_ea(const hop56::operand& op, addr_t& target_address);
};

class DisAnalyse56
{
public:
    // True if jsr
    static bool isSubroutine(const hop56::instruction& inst);

    // See if this is a branch and whether it would be taken
    static bool isBranch(const hop56::instruction &inst, const DspRegisters& regs, bool& takeBranch);

    // See if this is a branch instruction and where it points (i.e. its effective address)
    static bool getBranchTarget(const hop56::instruction &inst, uint32_t instAddr, uint32_t& target, bool& reversed);
};

#endif // DISASSEMBLER56_H
