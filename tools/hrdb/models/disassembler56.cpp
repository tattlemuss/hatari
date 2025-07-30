#include "disassembler56.h"
#include "hopper56/buffer56.h"
#include "stringformat.h"
#include "registers.h"

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

        // Save copy of instruction memory
        {
            uint16_t count = line.inst.word_count;
            if (count > 6)
                count = 6;
            int pos = 0;
            buffer_reader buf_copy(buf);
            for (int i = 0; i < count; ++i)
            {
                uint32_t tmp = 0;
                buf_copy.read_word(tmp);
                line.mem[pos++] = (tmp >> 16) & 0xff;
                line.mem[pos++] = (tmp >> 8) & 0xff;
                line.mem[pos++] = (tmp >> 0) & 0xff;
            }
        }

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

int Disassembler56::print_inst(const hop56::instruction &inst, QTextStream &ref)
{

    if (inst.opcode == hop56::INVALID)
    {
        ref << "dc     " << Format::to_hex32(inst.header);
        return 0;
    }

    QString opcode = hop56::get_opcode_string(inst.opcode);
    QString pad = QString("%1").arg(opcode, -6);    // I don't understand how this works
    ref << pad;

    QString part1;
    QTextStream ref1(&part1);

    // Initial operands
    for (int i = 0; i < 3; ++i)
    {
        const hop56::operand& op = inst.operands[i];
        if (op.type == hop56::operand::NONE)
            break;

        if (i == 0)
        {
            if (inst.neg_operands)
                ref1 << "-";
        }
        else
            ref1 << ",";

        print(op, ref1);
    }

    for (int i = 0; i < 2; ++i)
    {
        const hop56::operand& op = inst.operands2[i];
        if (op.type == hop56::operand::NONE)
            break;

        if (i == 0)
            ref1 << "   ";
        else
            ref1 << ",";

        print(op, ref1);
    }

    // Pad out the first set of operands
    ref1.flush();
    //if (part1.size() > 0)
    {
        pad = QString("%1").arg(part1, -9);
        ref << pad;
    }

    // Parallel moves
    for (int i = 0; i < 2; ++i)
    {
        const hop56::pmove& pmove = inst.pmoves[i];
        if (pmove.operands[0].type == hop56::operand::NONE)
            continue;	// skip if there is no first operand

        QString part2;
        QTextStream ref2(&part2);
        // Always ensure 1 space
        ref2 << " ";
        print(pmove.operands[0], ref2);

        if (pmove.operands[1].type != hop56::operand::NONE)
        {
            ref2 << ",";
            print(pmove.operands[1], ref2);
        }
        // Align this pmove part too
        pad = QString("%1").arg(part2, -12);
        ref << pad;
    }
    return 0;
}

// Check if an operand jumps to another known address, and return that address
bool Disassembler56::calc_ea(const hop56::operand& op, addr_t& target_address)
{
    if (op.type == hop56::operand::ABS)
    {
        target_address.mem = op.memory;
        target_address.addr = op.abs.address;
        return true;
    }
    if (op.type == hop56::operand::ABS_SHORT)
    {
        target_address.mem = op.memory;
        target_address.addr = op.abs_short.address;
        return true;
    }
    if (op.type == hop56::operand::IO_SHORT)
    {
        target_address.mem = op.memory;
        target_address.addr = op.io_short.address;
        return true;
    }

    return false;
}


int Disassembler56::print_terse(const hop56::instruction &inst, QTextStream &ref)
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
    return 0;
}

bool DisAnalyse56::isSubroutine(const hop56::instruction &inst)
{
    switch (inst.opcode)
    {
        case hop56::Opcode::O_JSCC:
        case hop56::Opcode::O_JSCLR:
        case hop56::Opcode::O_JSCS:
        case hop56::Opcode::O_JSEC:
        case hop56::Opcode::O_JSEQ:
        case hop56::Opcode::O_JSES:
        case hop56::Opcode::O_JSET:
        case hop56::Opcode::O_JSGE:
        case hop56::Opcode::O_JSGT:
        case hop56::Opcode::O_JSLC:
        case hop56::Opcode::O_JSLE:
        case hop56::Opcode::O_JSLS:
        case hop56::Opcode::O_JSLT:
        case hop56::Opcode::O_JSMI:
        case hop56::Opcode::O_JSNE:
        case hop56::Opcode::O_JSNN:
        case hop56::Opcode::O_JSNR:
        case hop56::Opcode::O_JSPL:
        case hop56::Opcode::O_JSR:
        case hop56::Opcode::O_JSSET:
            return true;
        default:
            break;
    }

    return false;
}

bool DisAnalyse56::isBranch(const hop56::instruction &inst, const DspRegisters &regs, bool &takeBranch)
{
    uint32_t sr = regs.Get(DspRegisters::SR);

    // See Page A-106 (Jcc) of the user manual
    // reg names are simplified to match the PDF easily
    bool L = (sr & (1<< DspRegisters::SRBits::kL)) != 0;
    bool E = (sr & (1<< DspRegisters::SRBits::kE)) != 0;
    bool U = (sr & (1<< DspRegisters::SRBits::kU)) != 0;
    bool N = (sr & (1<< DspRegisters::SRBits::kN)) != 0;
    bool Z = (sr & (1<< DspRegisters::SRBits::kZ)) != 0;
    bool V = (sr & (1<< DspRegisters::SRBits::kV)) != 0;
    bool C = (sr & (1<< DspRegisters::SRBits::kC)) != 0;
    switch (inst.opcode)
    {
        case hop56::Opcode::O_JCC:
            takeBranch = C == false;
            return true;
        case hop56::Opcode::O_JCS:
            takeBranch = C == true;
            return true;
        case hop56::Opcode::O_JEC:
            takeBranch = E == false;
            return true;
        case hop56::Opcode::O_JEQ:
            takeBranch = Z == true;
            return true;
        case hop56::Opcode::O_JES:
            takeBranch = E == true;
            return true;
        case hop56::Opcode::O_JGE:
            takeBranch = (N ^ V) == false;
            return true;
        case hop56::Opcode::O_JGT:
            takeBranch = (Z | (N ^ V)) == false;
            return true;
        case hop56::Opcode::O_JLC:
            takeBranch = L == false;
            return true;
        case hop56::Opcode::O_JLE:
            takeBranch = (Z | (N ^ V)) == true;
            return true;
        case hop56::Opcode::O_JLS:
            takeBranch = L == true;
            return true;
        case hop56::Opcode::O_JLT:
            takeBranch = (N ^ V) == true;
            return true;
        case hop56::Opcode::O_JMI:
            takeBranch = N == true;
            return true;
        case hop56::Opcode::O_JMP:
            takeBranch = true;
            return true;
        case hop56::Opcode::O_JNE:
            takeBranch = Z == false;
            return true;
        case hop56::Opcode::O_JNN:
            takeBranch = (Z | (!U & !E)) == true;
            return true;
        case hop56::Opcode::O_JNR:
            takeBranch = (Z | (!U & !E)) == false;
            return true;
        case hop56::Opcode::O_JPL:
            takeBranch = !N;
            return true;
        default:
            break;
    }
    return false;
}

bool DisAnalyse56::getBranchTarget(const hop56::instruction &inst, uint32_t instAddr, uint32_t &target, bool& reversed)
{
    reversed = false;
    switch (inst.opcode)
    {
    case hop56::Opcode::O_JCC:
    case hop56::Opcode::O_JCS:
    case hop56::Opcode::O_JEC:
    case hop56::Opcode::O_JEQ:
    case hop56::Opcode::O_JES:
    case hop56::Opcode::O_JGE:
    case hop56::Opcode::O_JGT:
    case hop56::Opcode::O_JLC:
    case hop56::Opcode::O_JLE:
    case hop56::Opcode::O_JLS:
    case hop56::Opcode::O_JLT:
    case hop56::Opcode::O_JMI:
    case hop56::Opcode::O_JNE:
    case hop56::Opcode::O_JNN:
    case hop56::Opcode::O_JNR:
    case hop56::Opcode::O_JPL:
    case hop56::Opcode::O_JMP:
    case hop56::Opcode::O_JSR:
        if (inst.operands[0].type == hop56::operand::Type::ABS)
        {
            target = inst.operands[0].abs.address;
            return true;
        }
        return false;

    case hop56::Opcode::O_JCLR:
    case hop56::Opcode::O_JSET:
        // e.g. jclr #1,<<$ffe9,addr
        if (inst.operands[2].type == hop56::operand::Type::ABS)
        {
            target = inst.operands[2].abs.address;
            return true;
        }
        return true;
    case hop56::Opcode::O_DO:
            // e.g. do #15,addr
            if (inst.operands[1].type == hop56::operand::Type::ABS)
            {
                target = inst.operands[1].abs.address;
                reversed = true;
                return true;
            }
            return true;
    case hop56::Opcode::O_REP:
        target = instAddr + inst.word_count;
        reversed = true;
        return true;
    default:
        break;
    }
    return false;
}
