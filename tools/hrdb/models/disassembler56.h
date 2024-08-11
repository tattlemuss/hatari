#ifndef DISASSEMBLER56_H
#define DISASSEMBLER56_H
#include "stdint.h"
#include <QVector>

#include "hopper56/decode.h"
#include "hopper56/instruction.h"

class QTextStream;
namespace hop56
{
    class buffer_reader;
}

class Disassembler56
{
public:
    struct line
    {
        uint32_t             address;
        hop56::instruction   inst;
        //uint8_t              mem[32];            // Copy of instruction memory

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
    static void print(const hop56::instruction& inst, /*const symbols& symbols, */ uint32_t inst_address, QTextStream& ref, bool bDisassHexNumerics );
};

#endif // DISASSEMBLER56_H
