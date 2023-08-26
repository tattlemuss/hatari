#ifndef DISASSEMBLER_H
#define DISASSEMBLER_H

#include <QVector>
#include <QTextStream>

#include "hopper/instruction.h"
#include "hopper/buffer.h"
#include "hopper/decode.h"

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
        QVector<line>    lines;
    };

    // Try to decode a single instruction
    static void decode_inst(buffer_reader& buf, instruction& inst, const decode_settings& settings);

    // Decode a block of instructions
    static int decode_buf(buffer_reader& buf, disassembly& disasm, const decode_settings& settings, uint32_t address, int32_t maxLines);

    // Format a single instruction and its arguments
    static void print(const instruction& inst, /*const symbols& symbols, */ uint32_t inst_address, QTextStream& ref, bool bDisassHexNumerics );

    // Format a single instruction and its arguments (no padding)
    static void print_terse(const instruction& inst, /*const symbols& symbols, */ uint32_t inst_address, QTextStream& ref, bool bDisassHexNumerics);

    // Find out the effective address of branch/jump, or for indirect addressing if "useRegs" is set.
    static bool calc_fixed_ea(const operand &operand, bool useRegs, const Registers& regs, uint32_t inst_address, uint32_t& ea);
};

class DisAnalyse
{
public:
    // True if bsr/jsr
    static bool isSubroutine(const instruction& inst);

    // True if trap, trapv
    static bool isTrap(const instruction& inst);
    static bool isBackDbf(const instruction& inst);

    // See if this is a branch and whether it would be taken
    static bool isBranch(const instruction &inst, const Registers& regs, bool& takeBranch);

    // See if this is a branch instruction and where it points (i.e. its effective address)
    static bool getBranchTarget(uint32_t instAddr, const instruction &inst, uint32_t& target);
};

#endif // DISASSEMBLER_H
