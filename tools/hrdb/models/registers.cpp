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

Registers::Registers()
{
    for (int i = 0; i < Registers::REG_COUNT; ++i)
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
