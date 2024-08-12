#include "disassembler56.h"
#include "hopper56/buffer.h"
#include "stringformat.h"

using namespace hop56;

#define REGNAME		hop56::get_register_string

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

// Print an operand, for all operand types
static void print(const hop56::operand& operand, /*const symbols& symbols,*/ QTextStream &ref)
{
    ref << hop56::get_memory_string(operand.memory);
    switch (operand.type)
    {
        case hop56::operand::IMM_SHORT:
            ref << "#" << operand.imm_short.val;
            break;
        case hop56::operand::REG:
            ref << REGNAME(operand.reg.index);
            break;
        case hop56::operand::POSTDEC_OFFSET:
            ref << QString::asprintf("(%s)-%s",
                    REGNAME(operand.postdec_offset.index_1),
                    REGNAME(operand.postdec_offset.index_2));
            break;
        case hop56::operand::POSTINC_OFFSET:
            ref << QString::asprintf("(%s)+%s",
                    REGNAME(operand.postinc_offset.index_1),
                    REGNAME(operand.postinc_offset.index_2));
            break;
        case hop56::operand::POSTDEC:
            ref << QString::asprintf("(%s)-",
                    REGNAME(operand.postdec.index));
            break;
        case hop56::operand::POSTINC:
            ref << QString::asprintf("(%s)+",
                    REGNAME(operand.postinc.index));
            break;
        case hop56::operand::NO_UPDATE:
            ref << QString::asprintf("(%s)",
                    REGNAME(operand.no_update.index));
            break;
        case hop56::operand::INDEX_OFFSET:
            ref << QString::asprintf("(%s+%s)",
                    REGNAME(operand.index_offset.index_1),
                    REGNAME(operand.index_offset.index_2));
            break;
        case hop56::operand::PREDEC:
            ref << QString::asprintf("-(%s)", REGNAME(operand.predec.index));
            break;
        case hop56::operand::ABS:
        {
#if 0
            symbol sym;
            if (find_symbol(symbols, hop56::Memory::MEM_P, operand.abs.address, sym))
                fprintf(pOutput, "%s", sym.label.c_str());
            else
#endif
                ref << QString::asprintf("$%x", operand.abs.address);
        }
            break;
        case hop56::operand::ABS_SHORT:
            ref << QString::asprintf(">$%x", operand.abs_short.address);
            break;
        case hop56::operand::IMM:
            ref << QString::asprintf("#$%x", operand.imm.val);
            break;
        case hop56::operand::IO_SHORT:
            ref << QString::asprintf("<<$%x", operand.io_short.address);
            break;
        default:
            ref << "??";
            break;
    }
}

int Disassembler56::print_terse(const hop56::instruction &inst, uint32_t inst_address, QTextStream &ref, bool bDisassHexNumerics)
{
    if (inst.opcode == hop56::INVALID)
    {
        ref << "DC " << Format::to_hex32(inst.header);
        return 0;
    }

    ref << hop56::get_opcode_string(inst.opcode);
    for (int i = 0; i < 3; ++i)
    {
        const hop56::operand& op = inst.operands[i];
        if (op.type == hop56::operand::NONE)
            break;

        if (i == 0)
        {
            ref << "   ";
            if (inst.neg_operands)
                ref << "-";
        }
        else
            ref << ",";

        print(op, ref);
    }

    for (int i = 0; i < 2; ++i)
    {
        const hop56::operand& op = inst.operands2[i];
        if (op.type == hop56::operand::NONE)
            break;

        if (i == 0)
            ref << "   ";
        else
            ref << ",";

        print(op, ref);
    }

    for (int i = 0; i < 2; ++i)
    {
        const hop56::pmove& pmove = inst.pmoves[i];
        if (pmove.operands[0].type == hop56::operand::NONE)
            continue;	// skip if there is no first operand

        ref << "   ";
        print(pmove.operands[0], ref);

        if (pmove.operands[1].type == hop56::operand::NONE)
            continue;	// next pmove
        ref << ",";
        print(pmove.operands[1], ref);
    }
}
