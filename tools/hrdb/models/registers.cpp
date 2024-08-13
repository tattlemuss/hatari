#include "registers.h"

//-----------------------------------------------------------------------------
// Careful! These names are used to send to Hatari too.
const char* Registers::s_names[] =
{
    "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
    "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
    "PC", "SR",
    "USP", "ISP",
    // Exception number
    "CAAR",
    "CACR",
    "DFC",
    "MSP",
    "SFC",
    "VBR",
    "EX",
    // Vars
    "AesOpcode",
    "Basepage",
    "BiosOpcode",
    "BSS",
    "CpuInstr",
    "CpuOpcodeType",
    "CycleCounter",
    "DATA",
    "DspInstr",
    "DspOpcodeType",
    "FrameCycles",
    "GemdosOpcode",
    "HBL",
    "LineAOpcode",
    "LineCycles",
    "LineFOpcode",
    "NextPC",
    "OsCallParam",
    "TEXT",
    "TEXTEnd",
    "VBL",
    "VdiOpcode",
    "XbiosOpcode",
    nullptr
};

const char* DspRegisters::s_names[] =
{
    "X1", "X0",
    "Y1", "Y0",
    "A2", "A1", "A0",
    "B2", "B1", "B0",
    "R0", "R1", "R2", "R3",
    "R4", "R5", "R6", "R7",
    "N0", "N1", "N2", "N3",
    "N4", "N5", "N6", "N7",
    "M0", "M1", "M2", "M3",
    "M4", "M5", "M6", "M7",
    "SR",
    "OMR",
    "SP",
    "SSH",
    "SSL",
    "LA",
    "LC",
    "A",
    "B",
    "X",
    "Y",
    "PC",
    nullptr
};

Registers::Registers()
{
    for (int i = 0; i < REG_COUNT; ++i)
        m_value[i] = 0;
}

const char *Registers::GetSRBitName(uint32_t bit)
{
    switch (bit)
    {
    case kTrace1: return "Trace1";
    case kTrace0: return "Trace0";
    case kSupervisor: return "Supervisor";
    case kIPL2: return "Interrupt Priority 2";
    case kIPL1: return "Interrupt Priority 1";
    case kIPL0: return "Interrupt Priority 0";
    case kX: return "eXtended Flag";
    case kN: return "Negative Flag";
    case kZ: return "Zero Flag";
    case kV: return "oVerflow Flag";
    case kC: return "Carry Flag";
    }
    return "";
}

const char* Registers::GetCACRBitName(uint32_t bit)
{
    switch (bit)
    {
    case WA: return "Write Allocate";
    case DBE: return "Data Burst Enable";
    case CD: return "Clear Data Cache";
    case CED: return "Clear Entry in Data Cache";
    case FD: return "Freeze Data Cache";
    case ED: return "Enable Data Cache";
    case IBE: return "Instruction Burst Enable";
    case CI: return "Clear Instruction Cache";
    case CEI: return "Clear Entry in Instruction Cache";
    case FI: return "Freeze Instruction Cache";
    case EI: return "Enable Instruction Cache";
    }
    return "";
}

DspRegisters::DspRegisters()
{
    for (int i = 0; i < REG_COUNT; ++i)
        m_value[i] = 0;
}

void DspRegisters::Set(uint32_t reg, uint64_t val)
{
    m_value[reg] = val;

    // Fix up combined register values
    switch (reg)
    {
    case A0: case A1: case A2:
        m_value[A] = (m_value[A2] << 48) | (m_value[A1] << 24) | m_value[A0]; break;
    case B0: case B1: case B2:
        m_value[B] = (m_value[B2] << 48) | (m_value[B1] << 24) | m_value[B0]; break;
    case X0: case X1:
        m_value[X] = (m_value[X1] << 24) | m_value[X0]; break;
    case Y0: case Y1:
        m_value[Y] = (m_value[Y1] << 24) | m_value[Y0]; break;
    }
}

const char* DspRegisters::GetSRBitName(uint32_t bit)
{
    switch (bit)
    {
    case kLF: return "Loop Flag";   // Loop
    case kDM: return "Double-Precision Multiply";
    case kT : return "Trace";
    case kS1: return "Scaling 1";
    case kS0: return "Scaling 0";
    case kI1: return "Interrupt Level 1";
    case kI0: return "Interrupt Level 0";
    case kS : return "Scaling";     // Scaling
    case kL : return "Limit";
    case kE : return "Extension";
    case kU : return "Unnormalized";
    case kN : return "Negative";
    case kZ : return "Zero";
    case kV : return "oVerflow";
    case kC : return "Carry";
    }
    return "";
}
