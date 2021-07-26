#ifndef DISASSEMBLER_H
#define DISASSEMBLER_H

#include <vector>
#include <QTextStream>

#include "hopper/instruction.h"
#include "hopper/buffer.h"

class SymbolTable;
class Registers;

class Disassembler
{
public:
    struct line
    {
        uint32_t    address;
        instruction inst;
        uint8_t     mem[10];            // Copy of instruction memory

        uint32_t    GetEnd() const
        {
            return address + inst.byte_count;
        }
    };

    // ----------------------------------------------------------------------------
    //	HIGHER-LEVEL DISASSEMBLY CREATION
    // ----------------------------------------------------------------------------
    // Storage for an attempt at tokenising the memory
    class disassembly
    {
    public:
        std::vector<line>    lines;
    };

    // Try to decode a single instruction
    static int decode_inst(buffer_reader &buf, instruction &inst);

    // Decode a block of instructions
    static int decode_buf(buffer_reader& buf, disassembly& disasm, uint32_t address, uint32_t maxLines);

    // Format a single instruction and its arguments
    static void print(const instruction& inst, /*const symbols& symbols, */ uint32_t inst_address, QTextStream& ref);

    // Find out the effective address of branch/jump, or for indirect addressing if "useRegs" is set.
    static bool calc_fixed_ea(const operand &operand, bool useRegs, const Registers& regs, uint32_t inst_address, uint32_t& ea);
};

class DisAnalyse
{
public:
    static bool isSubroutine(const instruction& inst);
    static bool isTrap(const instruction& inst);
};

#endif // DISASSEMBLER_H
