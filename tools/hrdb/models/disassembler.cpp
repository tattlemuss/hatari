#include "disassembler.h"

#include "hopper/buffer.h"
#include "hopper/instruction.h"
#include "hopper/decode.h"
#include "symboltable.h"
#include "registers.h"

static const char* instruction_names[Opcode::COUNT] =
{
    "none",
    "abcd",
    "add",
    "adda",
    "addi",
    "addq",
    "addx",
    "and",
    "andi",
    "asl",
    "asr",
    "bcc",
    "bchg",
    "bclr",
    "bcs",
    "beq",
    "bge",
    "bgt",
    "bhi",
    "ble",
    "bls",
    "blt",
    "bmi",
    "bne",
    "bpl",
    "bra",
    "bset",
    "bsr",
    "btst",
    "bvc",
    "bvs",
    "chk",
    "clr",
    "cmp",
    "cmpi",
    "cmpa",
    "cmpm",
    "dbcc",
    "dbcs",
    "dbeq",
    "dbf",
    "dbge",
    "dbgt",
    "dbhi",
    "dble",
    "dbls",
    "dblt",
    "dbmi",
    "dbne",
    "dbpl",
    "dbvc",
    "dbvs",
    "divs",
    "divu",
    "eor",
    "eori",
    "exg",
    "ext",
    "illegal",
    "jmp",
    "jsr",
    "lea",
    "link",
    "lsl",
    "lsr",
    "move",
    "movea",
    "movem",
    "movep",
    "moveq",
    "muls",
    "mulu",
    "nbcd",
    "neg",
    "negx",
    "nop",
    "not",
    "or",
    "ori",
    "pea",
    "reset",
    "rol",
    "ror",
    "roxl",
    "roxr",
    "rte",
    "rtr",
    "rts",
    "sbcd",
    "scc",
    "scs",
    "seq",
    "sf",
    "sge",
    "sgt",
    "shi",
    "sle",
    "sls",
    "slt",
    "smi",
    "sne",
    "spl",
    "st",
    "stop",
    "sub",
    "suba",
    "subi",
    "subq",
    "subx",
    "svc",
    "svs",
    "swap",
    "tas",
    "trap",
    "trapv",
    "tst",
    "unlk"
};


int Disassembler::decode_inst(buffer_reader& buf, instruction& inst)
{
    return decode(buf, inst);
}

int Disassembler::decode_buf(buffer_reader& buf, disassembly& disasm, uint32_t address, int32_t maxLines)
{
    while (buf.get_remain() >= 2)
    {
        line line;
        line.address = buf.get_pos() + address;

        // decode uses a copy of the buffer state
        {
            buffer_reader buf_copy(buf);
            decode(buf_copy, line.inst);
        }

        // Save copy of instruction memory
        {
            uint16_t count = line.inst.byte_count;
            if (count > 10)
                count = 10;

            buffer_reader buf_copy(buf);
            buf_copy.read(line.mem, count);
        }

        // Handle failure
        disasm.lines.push_back(line);

        buf.advance(line.inst.byte_count);
        if (disasm.lines.size() >= maxLines)
            break;
    }
    return 0;
}

// ----------------------------------------------------------------------------
//	INSTRUCTION ANALYSIS
// ----------------------------------------------------------------------------
// Check if an instruction jumps to another known address, and return that address
bool calc_relative_address(const operand& op, uint32_t inst_address, uint32_t& target_address)
{
    if (op.type == PC_DISP)
    {
        // Relative
        int16_t disp = op.pc_disp.disp;

        // The base PC is always +2 from the instruction address, since
        // the 68000 has already fetched the header word by then
        target_address = inst_address + 2 + disp;
        return true;
    }
    else if (op.type == PC_DISP_INDEX)
    {
        // Relative
        int8_t disp = op.pc_disp_index.disp;

        // The base PC is always +2 from the instruction address, since
        // the 68000 has already fetched the header word by then
        target_address = inst_address + 2 + disp;

        // Now apply the register offset

        return true;
    }
    if (op.type == RELATIVE_BRANCH)
    {
        // Relative
        int16_t disp = op.relative_branch.disp;

        // The base PC is always +2 from the instruction address, since
        // the 68000 has already fetched the header word by then
        target_address = inst_address + 2 + disp;
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
//	INSTRUCTION DISPLAY FORMATTING
// ----------------------------------------------------------------------------
QString to_hex32(uint32_t val)
{
    QString tmp;
    tmp= QString::asprintf("$%x", val);
    return tmp;
}

QString to_abs_word(uint16_t val)
{
    QString tmp;
    if (val & 0x8000)
        tmp = QString::asprintf("$ffff%04x", val);
    else {
        tmp = QString::asprintf("$%x", val);
    }
    return tmp;
}

void print_movem_mask(uint16_t reg_mask, QTextStream& ref)
{
    int num_ranges = 0;
    for (int regtype = 0; regtype < 2; ++regtype)   // D or A
    {
        char regname = regtype == 0 ? 'd' : 'a';

        // Select the 8 bits we care about
        uint16_t mask = ((reg_mask) >> (8 * regtype)) & 0xff;
        mask <<= 1; // shift up so that "-1" is a 0

        // an example mask for d0/d3-d7 is now:
        // reg: x76543210x   (x must be 0)
        // val: 0111110010

        // NOTE: 9 bits to simulate "d8" being 0 so
        // that a sequence ending in d7 can finish
        int lastStart = -1;
        for (int bitIdx = 0; bitIdx <= 8; ++bitIdx)
        {
            // Check for transitions by looking at each pair of bits in turn
            // Our "test" bit corresponding to bitIdx is the higher bit here
            uint16_t twoBits = (mask >> bitIdx) & 3;
            if (twoBits == 1)   // "%01"
            {
                // Last bit on, our new bit off, add a range
                // up to the previous bit ID (bitIdx - 1).
                // Add separator when required
                if (num_ranges != 0)
                    ref << "/";

                if (lastStart == bitIdx - 1)
                    ref << regname << lastStart;    // single register
                else
                    ref << regname << lastStart << "-" << regname << (bitIdx - 1);  // range
                ++num_ranges;
            }
            else if (twoBits == 2) // "%10"
            {
                // last bit off, new bit on, so this is start of a run from "bitIdx"
                lastStart = bitIdx;
            }
        }
    }
}

// ----------------------------------------------------------------------------
void print(const operand& operand, uint32_t inst_address, QTextStream& ref)
{
    switch (operand.type)
    {
        case OpType::D_DIRECT:
            ref << "d" << operand.d_register.reg;
            return;
        case OpType::A_DIRECT:
            ref << "a" << operand.a_register.reg;
            return;
        case OpType::INDIRECT:
            ref << "(a" << operand.indirect.reg << ")";
            return;
        case OpType::INDIRECT_POSTINC:
            ref << "(a" << operand.indirect_postinc.reg << ")+";
            return;
        case OpType::INDIRECT_PREDEC:
            ref << "-(a" << operand.indirect_predec.reg << ")";
            return;
        case OpType::INDIRECT_DISP:
            ref << operand.indirect_disp.disp << "(a" << operand.indirect_disp.reg << ")";
            return;
        case OpType::INDIRECT_INDEX:
            ref << operand.indirect_index.disp << "(a" << operand.indirect_index.a_reg <<
                   ",d" << operand.indirect_index.d_reg <<
                   (operand.indirect_index.is_long ? ".l" : ".w") <<
                   ")";
            return;
        case OpType::ABSOLUTE_WORD:
            ref << to_abs_word(operand.absolute_word.wordaddr) << ".w";
            return;
        case OpType::ABSOLUTE_LONG:
            ref << to_hex32(operand.absolute_long.longaddr);
            return;
        case OpType::PC_DISP:
        {
            uint32_t target_address;
            calc_relative_address(operand, inst_address, target_address);
            ref << to_hex32(target_address);
            ref << "(pc)";
            return;
        }
        case OpType::PC_DISP_INDEX:
        {
            uint32_t target_address;
            calc_relative_address(operand, inst_address, target_address);
            ref << to_hex32(target_address) << "(pc,d" << operand.pc_disp_index.d_reg <<
                   (operand.pc_disp_index.is_long ? ".l" : ".w") << ")";
            return;
        }
        case OpType::MOVEM_REG:
            print_movem_mask(operand.movem_reg.reg_mask, ref);
            return;
        case OpType::RELATIVE_BRANCH:
        {
            uint32_t target_address;
            calc_relative_address(operand, inst_address, target_address);
            ref << to_hex32(target_address);
            return;
        }
        case OpType::IMMEDIATE:
            ref << "#" << to_hex32(operand.imm.val0);
            return;
        case OpType::SR:
            ref << "sr";
            return;
        case OpType::USP:
            ref << "usp";
            return;
        case OpType::CCR:
            ref << "ccr";
            return;
        default:
            ref << "?";
    }
}

// ----------------------------------------------------------------------------
void Disassembler::print(const instruction& inst, /*const symbols& symbols, */ uint32_t inst_address, QTextStream& ref)
{
    if (inst.opcode == Opcode::NONE)
    {
        ref << "dc.w    " << to_hex32(inst.header);
        return;
    }
    QString opcode = instruction_names[inst.opcode];
    switch (inst.suffix)
    {
        case Suffix::BYTE:
            opcode += ".b"; break;
        case Suffix::WORD:
            opcode += ".w"; break;
        case Suffix::LONG:
            opcode += ".l"; break;
        case Suffix::SHORT:
            opcode += ".s"; break;
        default:
            break;
    }
    QString pad = QString("%1").arg(opcode, -8);
    ref << pad;

    if (inst.op0.type != OpType::INVALID)
    {
        ::print(inst.op0, /*symbols,*/ inst_address, ref);
    }

    if (inst.op1.type != OpType::INVALID)
    {
        ref << ",";
        ::print(inst.op1, /*symbols,*/ inst_address, ref);
    }
}

// ----------------------------------------------------------------------------
void Disassembler::print_terse(const instruction& inst, /*const symbols& symbols, */ uint32_t inst_address, QTextStream& ref)
{
    if (inst.opcode == Opcode::NONE)
    {
        ref << "dc.w " << to_hex32(inst.header);
        return;
    }
    QString opcode = instruction_names[inst.opcode];
    switch (inst.suffix)
    {
        case Suffix::BYTE:
            opcode += ".b"; break;
        case Suffix::WORD:
            opcode += ".w"; break;
        case Suffix::LONG:
            opcode += ".l"; break;
        case Suffix::SHORT:
            opcode += ".s"; break;
        default:
            break;
    }
    ref << opcode;

    if (inst.op0.type != OpType::INVALID)
    {
        ref << " ";
        ::print(inst.op0, /*symbols,*/ inst_address, ref);
    }

    if (inst.op1.type != OpType::INVALID)
    {
        ref << ",";
        ::print(inst.op1, /*symbols,*/ inst_address, ref);
    }
}

bool Disassembler::calc_fixed_ea(const operand &operand, bool useRegs, const Registers& regs, uint32_t inst_address, uint32_t& ea)
{
    switch (operand.type)
    {
    case OpType::D_DIRECT:
        return false;
    case OpType::A_DIRECT:
        return false;
    case OpType::INDIRECT:
        if (!useRegs)
            return false;
        ea = regs.GetAReg(operand.indirect.reg);
        return true;
    case OpType::INDIRECT_POSTINC:
        if (!useRegs)
            return false;
        ea = regs.GetAReg(operand.indirect_postinc.reg);
        return true;
    case OpType::INDIRECT_PREDEC:
        if (!useRegs)
            return false;
        ea = regs.GetAReg(operand.indirect_predec.reg);
        return true;
    case OpType::INDIRECT_DISP:
        if (!useRegs)
            return false;
        ea = regs.GetAReg(operand.indirect_disp.reg) + operand.indirect_disp.disp;
        return true;
    case OpType::INDIRECT_INDEX:
    {
        if (!useRegs)
            return false;
        uint32_t a_reg = regs.GetAReg(operand.indirect_index.a_reg);
        uint32_t d_reg = regs.GetDReg(operand.indirect_index.d_reg);
        int8_t disp = operand.indirect_index.disp;
        if (operand.indirect_index.is_long)
            ea = a_reg + d_reg + disp;
        else
            ea = a_reg + (int16_t)(d_reg & 0xffff) + disp;
        return true;
    }
    case OpType::ABSOLUTE_WORD:
        ea = operand.absolute_word.wordaddr;
        if (ea & 0x8000)
            ea |= 0xffff0000;     // extend to full EA
        return true;
    case OpType::ABSOLUTE_LONG:
        ea = operand.absolute_long.longaddr;
        return true;
    case OpType::PC_DISP:
        calc_relative_address(operand, inst_address, ea);
        return true;
    case OpType::PC_DISP_INDEX:
    {
        if (!useRegs)
            return false;
        // This generates the n(pc) part
        calc_relative_address(operand, inst_address, ea);
        uint32_t d_reg = regs.GetDReg(operand.pc_disp_index.d_reg);
        if (operand.pc_disp_index.is_long)
            ea += d_reg;
        else
            ea += (int16_t)(d_reg & 0xffff);
        return true;
    }
    case OpType::MOVEM_REG:
        return false;
    case OpType::RELATIVE_BRANCH:
        calc_relative_address(operand, inst_address, ea);
        return true;
    case OpType::IMMEDIATE:
        return false;
    case OpType::SR:
        return false;
    case OpType::USP:
        if (!useRegs)
            return false;
        ea = regs.m_value[Registers::USP];
        return true;
    case OpType::CCR:
        return false;
    default:
        return false;
    }
}
bool DisAnalyse::isSubroutine(const instruction &inst)
{
    switch (inst.opcode)
    {
        case Opcode::JSR:
        case Opcode::BSR:
            return true;
        default:
            break;
    }
    return false;
}

bool DisAnalyse::isTrap(const instruction &inst)
{
    switch (inst.opcode)
    {
        case Opcode::TRAP:
		case Opcode::TRAPV:
			return true;
        default:
            break;
    }
    return false;
}

bool DisAnalyse::isBackDbf(const instruction &inst)
{
	switch (inst.opcode)
	{
		case Opcode::DBF:
			if (inst.op1.type == OpType::RELATIVE_BRANCH)
				return inst.op1.relative_branch.disp <= 0;
			break;
		default:
			break;
	}
	return false;
}

static bool isDbValid(const instruction& inst, const Registers& regs)
{
	assert(inst.op0.type == OpType::D_DIRECT);
	uint32_t val = regs.GetDReg(inst.op0.d_register.reg);

	// When decremented return "OK" if the value doesn't become -1
	return val != 0;
}

static bool checkCC(uint8_t cc, uint32_t sr)
{
	int N = (sr >> 3) & 1;
	int Z = (sr >> 2) & 1;
	int V = (sr >> 1) & 1;
	int C = (sr >> 0) & 1;
	switch (cc)
	{
		case 0: return true;			// T
		case 1: return false;			// F
		case 2: return !C && !Z;		// HI
		case 3: return C || Z;			// LS
		case 4: return !C;				// CC
		case 5: return C;				// CS
		case 6: return !Z;				// NE
		case 7: return Z;				// EQ
		case 8: return !V;				// VC
		case 9: return V;				// VS
		case 10: return !N;				// PL
		case 11: return N;				// MI
			// These 4 are tricky. Used the implementation of "cctrue()" from Hatari:
		case 12: return (N^V) == 0;		// GE
		case 13: return (N^V) != 0;		// LT
		case 14: return !Z && (!(N^V));	// GT
		case 15: return ((N^V)|Z);		// LE
	}
	return false;
}

bool DisAnalyse::isBranch(const instruction &inst, const Registers& regs, bool& takeBranch)
{
	uint32_t sr = regs.Get(Registers::SR);
	//bool X = (sr >> 4) & 1;
	switch (inst.opcode)
	{
		case Opcode::BRA:		 takeBranch = checkCC(0, sr);		return true;
			// There is no "BF"
		case Opcode::BHI:		 takeBranch = checkCC(2, sr);		return true;
		case Opcode::BLS:		 takeBranch = checkCC(3, sr);		return true;
		case Opcode::BCC:		 takeBranch = checkCC(4, sr);		return true;
		case Opcode::BCS:		 takeBranch = checkCC(5, sr);		return true;
		case Opcode::BNE:        takeBranch = checkCC(6, sr);		return true;
		case Opcode::BEQ:        takeBranch = checkCC(7, sr);		return true;
		case Opcode::BVC:		 takeBranch = checkCC(8, sr);		return true;
		case Opcode::BVS:		 takeBranch = checkCC(9, sr);		return true;
		case Opcode::BPL:		 takeBranch = checkCC(10, sr);		return true;
		case Opcode::BMI:		 takeBranch = checkCC(11, sr);		return true;
		case Opcode::BGE:		 takeBranch = checkCC(12, sr);		return true;
		case Opcode::BLT:		 takeBranch = checkCC(13, sr);		return true;
		case Opcode::BGT:		 takeBranch = checkCC(14, sr);		return true; // !ZFLG && (NFLG == VFLG)
		case Opcode::BLE:		 takeBranch = checkCC(15, sr);		return true; // ZFLG || (NFLG != VFLG)

		// DBcc
		// Branch is taken when condition NOT valid and reg-- != -1

		//	If Condition False
		//	Then (Dn – 1 → Dn; If Dn ≠ – 1 Then PC + d n → PC)
		case Opcode::DBF:		 takeBranch = isDbValid(inst, regs) && !checkCC(1, sr);		return true;
		case Opcode::DBHI:		 takeBranch = isDbValid(inst, regs) && !checkCC(2, sr);		return true;
		case Opcode::DBLS:		 takeBranch = isDbValid(inst, regs) && !checkCC(3, sr);		return true;
		case Opcode::DBCC:		 takeBranch = isDbValid(inst, regs) && !checkCC(4, sr);		return true;
		case Opcode::DBCS:		 takeBranch = isDbValid(inst, regs) && !checkCC(5, sr);		return true;
		case Opcode::DBNE:       takeBranch = isDbValid(inst, regs) && !checkCC(6, sr);		return true;
		case Opcode::DBEQ:       takeBranch = isDbValid(inst, regs) && !checkCC(7, sr);		return true;
		case Opcode::DBVC:		 takeBranch = isDbValid(inst, regs) && !checkCC(8, sr);		return true;
		case Opcode::DBVS:		 takeBranch = isDbValid(inst, regs) && !checkCC(9, sr);		return true;
		case Opcode::DBPL:		 takeBranch = isDbValid(inst, regs) && !checkCC(10, sr);	return true;
		case Opcode::DBMI:		 takeBranch = isDbValid(inst, regs) && !checkCC(11, sr);	return true;
		case Opcode::DBGE:		 takeBranch = isDbValid(inst, regs) && !checkCC(12, sr);	return true;
		case Opcode::DBLT:		 takeBranch = isDbValid(inst, regs) && !checkCC(13, sr);	return true;
		case Opcode::DBGT:		 takeBranch = isDbValid(inst, regs) && !checkCC(14, sr);	return true;
		case Opcode::DBLE:		 takeBranch = isDbValid(inst, regs) && !checkCC(15, sr);	return true;

		default:
			break;
	}
	takeBranch = false;
	return false;
}
