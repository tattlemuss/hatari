#include "disassembler56.h"
#include "hopper56/buffer.h"
#include "stringformat.h"

using namespace hop56;
void Disassembler56::decode_inst(buffer_reader& buf, instruction& inst, const decode_settings& settings)
{
    decode(inst, buf, settings);
}

int Disassembler56::decode_buf(buffer_reader& buf, disassembly& disasm, const decode_settings& settings, uint32_t address, int32_t maxLines)
{
    while (buf.get_remain() >= 1)
    {
        line line;
        line.address = buf.get_pos() + address;

        // decode uses a copy of the buffer state
        {
            buffer_reader buf_copy(buf);
            decode(line.inst, buf_copy, settings);
        }

#if 0
        // Save copy of instruction memory
        {
            uint16_t count = line.inst.word_count;
            buffer_reader buf_copy(buf);
            buf_copy.read(line.mem, count);
        }
#endif
        // Handle failure
        disasm.lines.push_back(line);

        buf.advance(line.inst.word_count);
        if (disasm.lines.size() >= maxLines)
            break;
    }
    return 0;
}

int Disassembler56::print_terse(const hop56::instruction &inst, uint32_t inst_address, QTextStream &ref, bool bDisassHexNumerics)
{
    ref << "DC " << Format::to_hex32(inst.header);
    ref << "   " << inst_address << "  ";

    if (inst.opcode == hop56::INVALID)
    {
        ref << "DC " << Format::to_hex32(inst.header);
        return 0;
    }

    ref << hop56::get_opcode_string(inst.opcode);
#if 0
    for (int i = 0; i < 3; ++i)
    {
        const hop56::operand& op = inst.operands[i];
        if (op.type == hop56::operand::NONE)
            break;

        if (i == 0)
        {
            fprintf(pOutput, "\t");
            if (inst.neg_operands)
                fprintf(pOutput, "-");
        }
        else
            fprintf(pOutput, ",");

        print(op, symbols, pOutput);
    }

    for (int i = 0; i < 2; ++i)
    {
        const hop56::operand& op = inst.operands2[i];
        if (op.type == hop56::operand::NONE)
            break;

        if (i == 0)
            fprintf(pOutput, "\t");
        else
            fprintf(pOutput, ",");

        print(op, symbols, pOutput);
    }

    for (int i = 0; i < 2; ++i)
    {
        const hop56::pmove& pmove = inst.pmoves[i];
        if (pmove.operands[0].type == hop56::operand::NONE)
            continue;	// skip if there is no first operand

        fprintf(pOutput, "\t");
        print(pmove.operands[0], symbols, pOutput);

        if (pmove.operands[1].type == hop56::operand::NONE)
            continue;	// next pmove
        fprintf(pOutput, ",");
        print(pmove.operands[1], symbols, pOutput);
    }
#endif
}
