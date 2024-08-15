#ifndef MEMORY_H
#define MEMORY_H

#include <cstdint>
#include <assert.h>
#include "memaddr.h"

static const int kNumDisasmViews = 2;
static const int kNumMemoryViews = 4;

enum MemorySlot : int
{
    kNone,          // e.g. regs
    kMainPC,        // Memory around the stopped PC for the main view (to allow stepping etc)
    kMainDspPC,     // Memory around the stopped PC for the main view (DSP version)

    kDisasm0,       // general disassembly view memory (K slots)

    kMemoryView0 = kDisasm0 + kNumDisasmViews,   // general memory view memorys (K Slots)

    kGraphicsInspector = kMemoryView0 + kNumMemoryViews,    // gfx bitmap
    kGraphicsInspectorVideoRegs,                            // same as kVideo but synced with graphics inspector requests
    kGraphicsInspectorPalette,

    kHardwareWindowMmu,
    kHardwareWindowVideo,
    kHardwareWindowMfp,
    kHardwareWindowBlitter,
    kHardwareWindowMfpVecs,
    kHardwareWindowDmaSnd,

    kHardwareWindowStart = kHardwareWindowMmu,
    kHardwareWindowEnd = kHardwareWindowDmaSnd,

    kBasePage,          // Bottom 256 bytes for vectors
    kMemorySlotCount
};

// Check if 2 memory ranges overlap
bool Overlaps(uint32_t addr1, uint32_t size1, uint32_t addr2, uint32_t size);

// A block of memory pulled from the target.
// The data is byte-based, so if you are reading from DSP data,
// you need to factor this in (there is no compensation made)
class Memory
{
public:
    Memory(MemSpace space, uint32_t addr, uint32_t sizeInBytes);

    ~Memory();

    void Clear();

    // Set a single byte.
    void Set(uint32_t offset, uint8_t val)
    {
        assert(offset < m_sizeInBytes);
        m_pData[offset] = val;
    }

    // Fetch from a byte offset
    uint8_t Get(uint32_t offset) const
    {
        assert(offset < m_sizeInBytes);
        return m_pData[offset];
    }

    MemAddr GetMemAddr() const
    {
        return maddr(m_space, m_addr);
    }

    MemSpace GetSpace() const
    {
        return m_space;
    }

    // Return the base address number.
    uint32_t GetAddress() const
    {
        return m_addr;
    }

    // Check if all of a given memory range is contained in this memblock.
    bool HasCpuRange(uint32_t address, uint32_t numBytes) const
    {
        // These calculations only make sense for a CPU block,
        // since addresses are wrong
        assert(m_space == MEM_CPU);

        // Shift the values in to a range of offsets.

        // Since we use unsigned arithmtic, at this point, any
        // "offset >= m_size" is out-of-range, even for addresses
        // below the start of the memory block, which makes
        // comparison simpler.
        // This logic should also work for memory addresses which wrap
        // around, for whatever reason.
        uint32_t offset = address - m_addr;
        if (offset >= m_sizeInBytes)
            return false;       // start address is too low

        // This check is ">" since ">=" matches the range fitting exactly.
        if (offset + numBytes > m_sizeInBytes)
            return false;       // end address is too high

        return true;
    }

    bool ReadCpuByte(uint32_t address, uint8_t& value) const
    {
        assert(m_space == MEM_CPU);
        value = 0U;
        if (!HasCpuRange(address, 1U))
            return false;
        uint32_t offset = address - m_addr;
        value = m_pData[offset];
        return true;
    }

    // Read multiple bytes and put into 32-bit word. So can read byte/word/long
    bool ReadCpuMulti(uint32_t address, uint32_t numBytes, uint32_t& value) const;

    // Returns the size of the memory in bytes (not DSP words!)
    uint32_t GetSize() const
    {
        return m_sizeInBytes;
    }

    const uint8_t* GetData() const
    {
        return m_pData;
    }

    // Deep copy of the data for caching.
    Memory& operator=(const Memory& other);

private:
    Memory(const Memory& other);	// hide to prevent accidental use

    MemSpace    m_space;
    uint32_t	m_addr;
    uint32_t	m_sizeInBytes;
    uint8_t*	m_pData;
};

#endif // MEMORY_H
