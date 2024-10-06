#include "disassembler.h"

#include "hopper/buffer.h"
#include "hopper/instruction.h"
#include "hopper/decode.h"
#include "stringformat.h"
#include "symboltable.h"
#include "registers.h"

using namespace hop68;

void Disassembler::decode_inst(buffer_reader& buf, instruction& inst, const decode_settings& settings)
{
    decode(inst, buf, settings);
}

int Disassembler::decode_buf(buffer_reader& buf, disassembly& disasm, const decode_settings& settings, uint32_t address, int32_t maxLines)
{
    while (buf.get_remain() >= 2)
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

static uint32_t GetIndexRegisterVal(const Registers& regs, IndexRegister indexReg)
{
    switch (indexReg)
    {
    case IndexRegister::INDEX_REG_A0: return regs.A0;
    case IndexRegister::INDEX_REG_A1: return regs.A1;
    case IndexRegister::INDEX_REG_A2: return regs.A2;
    case IndexRegister::INDEX_REG_A3: return regs.A3;
    case IndexRegister::INDEX_REG_A4: return regs.A4;
    case IndexRegister::INDEX_REG_A5: return regs.A5;
    case IndexRegister::INDEX_REG_A6: return regs.A6;
    case IndexRegister::INDEX_REG_A7: return regs.A7;
    case IndexRegister::INDEX_REG_D0: return regs.D0;
    case IndexRegister::INDEX_REG_D1: return regs.D1;
    case IndexRegister::INDEX_REG_D2: return regs.D2;
    case IndexRegister::INDEX_REG_D3: return regs.D3;
    case IndexRegister::INDEX_REG_D4: return regs.D4;
    case IndexRegister::INDEX_REG_D5: return regs.D5;
    case IndexRegister::INDEX_REG_D6: return regs.D6;
    case IndexRegister::INDEX_REG_D7: return regs.D7;
    default: break;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Check if an instruction jumps to another known address, and return that address
static bool calc_relative_address(const operand& op, uint32_t inst_address, uint32_t& target_address)
{
    if (op.type == PC_DISP)
    {
        target_address = inst_address + op.pc_disp.inst_disp;
        return true;
    }
    else if (op.type == PC_DISP_INDEX)
    {
        target_address = inst_address + op.pc_disp_index.inst_disp;
        return true;
    }
    else if (op.type == RELATIVE_BRANCH)
    {
        target_address = inst_address + op.relative_branch.inst_disp;
        return true;
    }
    else if (op.type == INDIRECT_POSTINDEXED || op.type == INDIRECT_PREINDEXED ||
             op.type == MEMORY_INDIRECT || op.type == NO_MEMORY_INDIRECT)
    {
        if (op.indirect_index_68020.base_register == INDEX_REG_PC)
        {
            // Special case since PC not always equal to instruction address at this time
            target_address = inst_address + op.indirect_index_68020.base_displacement;
            return true;
        }
    }

    return false;
}

// ----------------------------------------------------------------------------
static void print_movem_mask(uint16_t reg_mask, QTextStream& ref)
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
static void print_index_indirect(const index_indirect& ind, QTextStream& ref)
{
    if (ind.index_reg == INDEX_REG_NONE)
        return;
    ref << get_index_register_string(ind.index_reg) << "." <<
        (ind.is_long ? "l" : "w") << get_scale_shift_string(ind.scale_shift);
}

// ----------------------------------------------------------------------------
static void print_bitfield_number(uint8_t is_reg, uint8_t offset, QTextStream& txt)
{
    if (is_reg)
        txt << "d" << (offset & 7);
    else
        txt << offset;
}

// ----------------------------------------------------------------------------
static void print_bitfield(const bitfield& bf, QTextStream& txt)
{
    txt << "{";
    print_bitfield_number(bf.offset_is_dreg, bf.offset, txt);
    txt << ":";
    print_bitfield_number(bf.width_is_dreg, bf.width, txt);
    txt << "}";
}

// ----------------------------------------------------------------------------
enum LastOutput
{
    kNone,			// start token
    kValue,			// number or similar
    kComma,			// ','
    kOpenBrace,		// '['
    kCloseBrace		// ']'
};

// ----------------------------------------------------------------------------
static void open_brace(LastOutput& last, bool& is_brace_open, QTextStream& txt)
{
    if (!is_brace_open)
    {
        txt << "[";
        is_brace_open = true;
        last = kOpenBrace;
    }
}

// ----------------------------------------------------------------------------
static void close_brace(LastOutput& last, bool& is_brace_open, QTextStream& txt)
{
    if (is_brace_open)
    {
        txt << "]";
        last = kCloseBrace;
    }
    //else
    //{
    //	// Insert an empty region
    //	fprintf(pFile, "[0]");
    //	last = kCloseBrace;
    //}
    is_brace_open = false;
}

// ----------------------------------------------------------------------------
static void insert_comma(LastOutput& last, QTextStream& txt)
{
    if (last == kValue || last == kCloseBrace)
    {
        txt << ",";
        last = kComma;
    }
}

// ----------------------------------------------------------------------------
static void print_indexed_68020(const indirect_index_full& ref,
    int brace_open, int brace_close, uint32_t inst_address, QTextStream& txt)
{
    txt << "(";
    LastOutput last = kNone;
    bool is_brace_open = false;
    for (int index = 0; index < 4; ++index)
    {
        if (ref.used[index])
        {
            // if an item is printed, we might need to open a brace or
            // insert a separating comma
            if (index >= brace_open && index <= brace_close)
                open_brace(last, is_brace_open, txt);
            insert_comma(last, txt);
            switch (index)
            {
                case 0:
                    if (ref.base_register == IndexRegister::INDEX_REG_PC)
                    {
                        // Decode PC-relative addresses
                        uint32_t address = ref.base_displacement + inst_address;
                        txt << Format::to_hex32(address);
                    }
                    else
                        txt << Format::to_hex32(ref.base_displacement);
                    break;
                case 1:
                    txt << get_index_register_string(ref.base_register); break;
                case 2:
                    print_index_indirect(ref.index, txt); break;
                case 3:
                    txt << Format::to_hex32(ref.outer_displacement); break;
            }
            last = kValue;
        }
        // Brace might need to be closed whether even if a new value wasn't printed
        if (index == brace_close)
            close_brace(last, is_brace_open, txt);
    }
    txt << ")";
}

// ----------------------------------------------------------------------------
static void print(const operand& operand, uint32_t inst_address, QTextStream& ref, bool bDisassHexNumerics = false)
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
            ref << Format::to_signed(operand.indirect_disp.disp, bDisassHexNumerics) << "(a" << operand.indirect_disp.reg << ")";
            return;
        case OpType::INDIRECT_INDEX:
            ref << Format::to_signed(operand.indirect_index.disp, bDisassHexNumerics) << "(a" << operand.indirect_index.a_reg << ",";
            print_index_indirect(operand.indirect_index.indirect_info, ref);
            ref << ")";
            return;
        case OpType::ABSOLUTE_WORD:
            ref << Format::to_abs_word(operand.absolute_word.wordaddr) << ".w";
            return;
        case OpType::ABSOLUTE_LONG:
            ref << Format::to_hex32(operand.absolute_long.longaddr);
            return;
        case OpType::PC_DISP:
        {
            uint32_t target_address;
            calc_relative_address(operand, inst_address, target_address);
            ref << Format::to_hex32(target_address);
            ref << "(pc)";
            return;
        }
        case OpType::PC_DISP_INDEX:
        {
            uint32_t target_address;
            calc_relative_address(operand, inst_address, target_address);
            ref << Format::to_hex32(target_address) << "(pc,";
            print_index_indirect(operand.pc_disp_index.indirect_info, ref);
            ref << ")";
            return;
        }
        case OpType::MOVEM_REG:
            print_movem_mask(operand.movem_reg.reg_mask, ref);
            return;
        case OpType::RELATIVE_BRANCH:
        {
            uint32_t target_address;
            calc_relative_address(operand, inst_address, target_address);
            ref << Format::to_hex32(target_address);
            return;
        }
        case OpType::IMMEDIATE:
            ref << "#" << Format::to_hex32(operand.imm.val0);
            return;
    case OpType::INDIRECT_POSTINDEXED:
        print_indexed_68020(operand.indirect_index_68020, 0, 1, inst_address, ref);
        return;
    case OpType::INDIRECT_PREINDEXED:
        print_indexed_68020(operand.indirect_index_68020, 0, 2, inst_address, ref);
        return;
    case OpType::MEMORY_INDIRECT:
        // This is the same as postindexed, except IS is suppressed!
        print_indexed_68020(operand.indirect_index_68020, 0, 1, inst_address, ref);
        return;
    case OpType::NO_MEMORY_INDIRECT:
        print_indexed_68020(operand.indirect_index_68020, -1, -1, inst_address, ref);
        return;
    case OpType::D_REGISTER_PAIR:
        ref << "d" << operand.d_register_pair.dreg1 << ":d" << operand.d_register_pair.dreg2;
        return;
    case OpType::INDIRECT_REGISTER_PAIR:
        ref << "("
            << get_index_register_string(operand.indirect_register_pair.reg1)
            << "):("
            << get_index_register_string(operand.indirect_register_pair.reg2)
            << ")";
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
        case OpType::CONTROL_REGISTER:
            ref << get_control_register_string(operand.control_register.cr);
            return;
        default:
            ref << "?";
    }
}

// ----------------------------------------------------------------------------
void Disassembler::print(const instruction& inst, /*const symbols& symbols, */ uint32_t inst_address, QTextStream& ref, bool bDisassHexNumerics = false )
{
    if (inst.opcode == Opcode::NONE)
    {
        ref << "dc.w     " << Format::to_hex32(inst.header);
        return;
    }
    QString opcode = get_opcode_string(inst.opcode);
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
    QString pad = QString("%1").arg(opcode, -9);
    ref << pad;

    if (inst.op0.type != OpType::INVALID)
    {
        ::print(inst.op0, /*symbols,*/ inst_address, ref, bDisassHexNumerics);
    }

    if (inst.bf0.valid)
        print_bitfield(inst.bf0, ref);

    if (inst.op1.type != OpType::INVALID)
    {
        ref << ",";
        ::print(inst.op1, inst_address, ref, bDisassHexNumerics);
    }
    if (inst.bf1.valid)
        print_bitfield(inst.bf1, ref);

    if (inst.op2.type != OpType::INVALID)
    {
        ref << ",";
        ::print(inst.op2, inst_address, ref, bDisassHexNumerics);
    }
}

// ----------------------------------------------------------------------------
void Disassembler::print_terse(const instruction& inst, /*const symbols& symbols, */ uint32_t inst_address, QTextStream& ref, bool bDisassHexNumerics = false)
{
    if (inst.opcode == Opcode::NONE)
    {
        ref << "dc.w " << Format::to_hex32(inst.header);
        return;
    }
    QString opcode = get_opcode_string(inst.opcode);
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
        ::print(inst.op0, /*symbols,*/ inst_address, ref, bDisassHexNumerics);
    }

    if (inst.op1.type != OpType::INVALID)
    {
        ref << ",";
        ::print(inst.op1, /*symbols,*/ inst_address, ref, bDisassHexNumerics);
    }
}

// ----------------------------------------------------------------------------
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
        uint32_t d_reg = GetIndexRegisterVal(regs, operand.indirect_index.indirect_info.index_reg);

        int8_t disp = operand.indirect_index.disp;
        int scale = 1 << operand.indirect_index.indirect_info.scale_shift;
        if (operand.indirect_index.indirect_info.is_long)
            ea = a_reg + d_reg * scale + disp;
        else
            ea = a_reg + (int16_t)(d_reg & 0xffff) * scale + disp;
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
        // This generates the n(pc) part
        calc_relative_address(operand, inst_address, ea);
        if (!useRegs)
            return true;            // Just display the base address for EA

        // Add the register value if we know it
        uint32_t d_reg = GetIndexRegisterVal(regs, operand.pc_disp_index.indirect_info.index_reg);
        if (operand.pc_disp_index.indirect_info.is_long)
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
    case OpType::NO_MEMORY_INDIRECT:
    case OpType::MEMORY_INDIRECT:
    case OpType::INDIRECT_POSTINDEXED:
    case OpType::INDIRECT_PREINDEXED:
    {
        // Do the best without supplying reg data
        bool res = calc_relative_address(operand, inst_address, ea);
        if (useRegs)
        {
            if (operand.type == OpType::NO_MEMORY_INDIRECT)
            {
                // This is the only one we can calc with a secondary memory fetch
                uint32_t base_reg_val = GetIndexRegisterVal(regs, operand.indirect_index_68020.base_register);
                uint32_t index_reg_val = GetIndexRegisterVal(regs, operand.indirect_index_68020.index.index_reg);
                int32_t disp = operand.indirect_index_68020.base_displacement;
                int scale = 1 << operand.indirect_index_68020.index.scale_shift;
                if (operand.indirect_index_68020.index.is_long)
                    ea = base_reg_val + index_reg_val * scale + disp;
                else
                    ea = base_reg_val + (int16_t)(index_reg_val & 0xffff) * scale + disp;
                return true;
            }
        }
        return res;
    }
    default:
        return false;
    }
}
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
bool DisAnalyse::isBackDbf(const instruction &inst)
{
	switch (inst.opcode)
	{
		case Opcode::DBF:
			if (inst.op1.type == OpType::RELATIVE_BRANCH)
                return inst.op1.relative_branch.inst_disp <= 0;
			break;
		default:
			break;
	}
	return false;
}

// ----------------------------------------------------------------------------
static bool isDbValid(const instruction& inst, const Registers& regs)
{
	assert(inst.op0.type == OpType::D_DIRECT);
	uint32_t val = regs.GetDReg(inst.op0.d_register.reg);

	// When decremented return "OK" if the value doesn't become -1
	return val != 0;
}

// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
bool DisAnalyse::getBranchTarget(uint32_t instAddr, const instruction &inst, uint32_t &target)
{
    switch (inst.opcode)
    {
        case Opcode::BRA:		 calc_relative_address(inst.op0, instAddr, target);		return true;
            // There is no "BF"
        case Opcode::BHI:		 calc_relative_address(inst.op0, instAddr, target);		return true;
        case Opcode::BLS:		 calc_relative_address(inst.op0, instAddr, target);		return true;
        case Opcode::BCC:		 calc_relative_address(inst.op0, instAddr, target);		return true;
        case Opcode::BCS:		 calc_relative_address(inst.op0, instAddr, target);		return true;
        case Opcode::BNE:        calc_relative_address(inst.op0, instAddr, target);		return true;
        case Opcode::BEQ:        calc_relative_address(inst.op0, instAddr, target);		return true;
        case Opcode::BVC:		 calc_relative_address(inst.op0, instAddr, target);		return true;
        case Opcode::BVS:		 calc_relative_address(inst.op0, instAddr, target);		return true;
        case Opcode::BPL:		 calc_relative_address(inst.op0, instAddr, target);		return true;
        case Opcode::BMI:		 calc_relative_address(inst.op0, instAddr, target);		return true;
        case Opcode::BGE:		 calc_relative_address(inst.op0, instAddr, target);		return true;
        case Opcode::BLT:		 calc_relative_address(inst.op0, instAddr, target);		return true;
        case Opcode::BGT:		 calc_relative_address(inst.op0, instAddr, target);		return true;
        case Opcode::BLE:		 calc_relative_address(inst.op0, instAddr, target);		return true;

        // DBcc
        case Opcode::DBF:		 calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBHI:		 calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBLS:		 calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBCC:		 calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBCS:		 calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBNE:       calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBEQ:       calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBVC:		 calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBVS:		 calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBPL:		 calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBMI:		 calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBGE:		 calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBLT:		 calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBGT:		 calc_relative_address(inst.op1, instAddr, target);		return true;
        case Opcode::DBLE:		 calc_relative_address(inst.op1, instAddr, target);		return true;

        default:
            break;
    }
    return false;
}
