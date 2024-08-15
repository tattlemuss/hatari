#include "hardware_st.h"
#include "regs_st.h"

#include "../models/memory.h"
#include "../models/targetmodel.h"

namespace HardwareST
{
    bool GetVideoBase(const Memory& mem, MACHINETYPE machineType, uint32_t& address)
    {
        uint32_t hi, mi, lo;
        if (!mem.ReadCpuMulti(Regs::VID_BASE_HIGH, 1, hi))
            return false;
        if (!mem.ReadCpuMulti(Regs::VID_BASE_MID, 1, mi))
            return false;
        lo = 0;
        if (!IsMachineST(machineType))
        {
            if (!mem.ReadCpuMulti(Regs::VID_BASE_LOW_STE, 1, lo))
                return false;
        }
        address = (hi << 16) | (mi << 8) | lo;
        return true;
    }

    bool GetVideoCurrent(const Memory& mem, uint32_t& address)
    {
        uint32_t hi, mi, lo;
        if (!mem.ReadCpuMulti(Regs::VID_CURR_HIGH, 1, hi))
            return false;
        if (!mem.ReadCpuMulti(Regs::VID_CURR_MID, 1, mi))
            return false;
        if (!mem.ReadCpuMulti(Regs::VID_CURR_LOW, 1, lo))
            return false;
        address = (hi << 16) | (mi << 8) | lo;
        return true;
    }

    bool GetBlitterSrc(const Memory &mem, MACHINETYPE machineType, uint32_t &address)
    {
        if (IsMachineST(machineType))
            return false;

        uint32_t val;
        if (!mem.ReadCpuMulti(Regs::BLT_SRC_ADDR, 4, val))
            return false;
        address = val & 0xffffff;
        return true;
    }

    bool GetBlitterDst(const Memory &mem, MACHINETYPE machineType, uint32_t &address)
    {
        if (IsMachineST(machineType))
            return false;
        uint32_t val = 0;
        if (!mem.ReadCpuMulti(Regs::BLT_DST_ADDR, 4, val))
            return false;
        address = val & 0xffffff;
        return true;
    }

    bool GetDmaStart(const Memory &mem, MACHINETYPE machineType, uint32_t &address)
    {
        if (!IsMachineSTE(machineType))
            return false;
        uint32_t hi, mi, lo;
        if (!mem.ReadCpuMulti(Regs::DMA_START_HIGH, 1, hi))
            return false;
        if (!mem.ReadCpuMulti(Regs::DMA_START_MID, 1, mi))
            return false;
        if (!mem.ReadCpuMulti(Regs::DMA_START_LOW, 1, lo))
            return false;
        address = (hi << 16) | (mi << 8) | lo;
        return true;
    }
    bool GetDmaCurr(const Memory &mem, MACHINETYPE machineType, uint32_t &address)
    {
        if (!IsMachineSTE(machineType))
            return false;
        uint32_t hi, mi, lo;
        if (!mem.ReadCpuMulti(Regs::DMA_CURR_HIGH, 1, hi))
            return false;
        if (!mem.ReadCpuMulti(Regs::DMA_CURR_MID, 1, mi))
            return false;
        if (!mem.ReadCpuMulti(Regs::DMA_CURR_LOW, 1, lo))
            return false;
        address = (hi << 16) | (mi << 8) | lo;
        return true;
    }
    bool GetDmaEnd(const Memory &mem, MACHINETYPE machineType, uint32_t &address)
    {
        if (!IsMachineSTE(machineType))
            return false;
        uint32_t hi, mi, lo;
        if (!mem.ReadCpuMulti(Regs::DMA_END_HIGH, 1, hi))
            return false;
        if (!mem.ReadCpuMulti(Regs::DMA_END_MID, 1, mi))
            return false;
        if (!mem.ReadCpuMulti(Regs::DMA_END_LOW, 1, lo))
            return false;
        address = (hi << 16) | (mi << 8) | lo;
        return true;
    }

    void GetColour(uint16_t regValue, MACHINETYPE machineType, uint32_t &result)
    {
        static const uint32_t stToRgb[16] =
        {
            0x00, 0x22, 0x44, 0x66, 0x88, 0xaa, 0xcc, 0xee,
            0x00, 0x22, 0x44, 0x66, 0x88, 0xaa, 0xcc, 0xee
        };
        static const uint32_t steToRgb[16] =
        {
            0x00, 0x22, 0x44, 0x66, 0x88, 0xaa, 0xcc, 0xee,
            0x11, 0x33, 0x55, 0x77, 0x99, 0xbb, 0xdd, 0xff
        };

        bool isST = IsMachineST(machineType);
        const uint32_t* pPalette = isST ? stToRgb : steToRgb;
        uint32_t  r = (regValue >> 8) & 0xf;
        uint32_t  g = (regValue >> 4) & 0xf;
        uint32_t  b = (regValue >> 0) & 0xf;

        uint32_t colour = 0xff000000U;
        colour |= pPalette[r] << 16;
        colour |= pPalette[g] << 8;
        colour |= pPalette[b] << 0;
        result = colour;
    }
}
