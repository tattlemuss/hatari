#ifndef REGISTERS_H
#define REGISTERS_H

#include <stdint.h>
#include <assert.h>

#define GET_REG(regs, index)    regs.Get(Registers::index)

/*
 * As well as registers, this contains many useful Hatari variables,
 * like cycle counts, HBL/VBL
 */
class Registers
{
public:
    Registers();

    // Useful accessors
    uint32_t Get(uint32_t reg) const
    {
        assert(reg < REG_COUNT);
        return m_value[reg];
    }
    uint32_t GetDReg(uint32_t index) const
    {
        assert(index <= 7);
        return m_value[D0 + index];
    }
    uint32_t GetAReg(uint32_t index) const
    {
        assert(index <= 7);
        return m_value[A0 + index];
    }
    static const char* GetSRBitName(uint32_t bit);
    static const char* GetCACRBitName(uint32_t bit);

    enum
    {
        D0 = 0,
        D1,
        D2,
        D3,
        D4,
        D5,
        D6,
        D7,
        A0,
        A1,
        A2,
        A3,
        A4,
        A5,
        A6,
        A7,
        PC,
        SR,
        USP,
        ISP,

        // 68020+
        CAAR,
        CACR,
        DFC,
        MSP,
        SFC,
        VBR,

        // Exception number
        EX,

        // Variables
        AesOpcode,
        Basepage,
        BiosOpcode,
        BSS,
        CpuInstr,
        CpuOpcodeType,
        CycleCounter,
        DATA,
        DspInstr,
        DspOpcodeType,
        FrameCycles,
        GemdosOpcode,
        HBL,
        LineAOpcode,
        LineCycles,
        LineFOpcode,
        NextPC,
        OsCallParam,
        TEXT,
        TEXTEnd,
        VBL,
        VdiOpcode,
        XbiosOpcode,
        REG_COUNT
    };

    enum SRBits
    {
        kTrace1 = 15,
        kTrace0 = 14,
        kSupervisor = 13,
        kIPL2 = 10,
        kIPL1 = 9,
        kIPL0 = 8,
        kX = 4,
        kN = 3,
        kZ = 2,
        kV = 1,
        kC = 0
    };

    enum CACRBits
    {
        WA = 13,    // Write allocate

        DBE = 12,   // Data Burst Enable
        CD = 11,    // Clear Data Cache
        CED = 10,   // Clear Entry in Data Cache
        FD = 9,     // Freeze Data Cache
        ED = 8,     // Enable Data Cache

        IBE = 4,    // Instruction Burst Enable
        CI = 3,     // Clear Instruction Cache
        CEI = 2,    // Clear Entry in Instruction Cache
        FI = 1,     // Freeze Instruction Cache
        EI = 0      // Enable Instruction Cache
    };

    uint32_t	m_value[REG_COUNT];
    // Null-terminated 1:1 array of register names
    static const char* s_names[];
};

class DspRegisters
{
public:
    DspRegisters();

    // Useful accessors
    uint64_t Get(uint32_t reg) const
    {
        assert(reg < REG_COUNT);
        return m_value[reg];
    }

    static const char* GetSRBitName(uint32_t bit);

    void Set(uint32_t reg, uint64_t val);

    enum
    {
        // WARNING this order does not match the Hatari runtime.
        X1, X0,
        Y1, Y0,
        A2, A1, A0,
        B2, B1, B0,
        R0, R1, R2, R3,
        R4, R5, R6, R7,
        N0, N1, N2, N3,
        N4, N5, N6, N7,
        M0, M1, M2, M3,
        M4, M5, M6, M7,
        SR,
        OMR,
        SP,
        SSH,
        SSL,
        LA,
        LC,

        // Pseudo registers calculated from the above.
        A,
        B,
        X,
        Y,
        PC,
        REG_COUNT
    };

    enum SRBits
    {
        kLF = 15,   // Loop
        kDM = 14,   // Double precision multiply?
        kT = 13,
        kS1 = 11,
        kS0 = 10,
        kI1 = 9,
        kI0 = 8,
        kS = 7,     // Scaling
        kL = 6,
        kE = 5,
        kU = 4,
        kN = 3,
        kZ = 2,
        kV = 1,
        kC = 0
    };

    // Null-terminated 1:1 array of register names
    static const char* s_names[];
private:
    uint64_t	m_value[REG_COUNT];

};

#endif // REGISTERS_H
