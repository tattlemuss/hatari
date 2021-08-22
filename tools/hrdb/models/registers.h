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
        EX,			// Exception number

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

    uint32_t	m_value[REG_COUNT];
    // Null-terminated 1:1 array of register names
    static const char* s_names[];

};

#endif // REGISTERS_H
