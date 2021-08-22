#ifndef MEMORY_H
#define MEMORY_H

#include <cstdint>
#include <assert.h>

static const int kNumDisasmViews = 2;
static const int kNumMemoryViews = 4;

enum MemorySlot
{
    kNone,          // e.g. regs
    kMainPC,        // Memory around the stopped PC for the main view (to allow stepping etc)

    kVideo,         // memory from $ff8200 to $ff8270

    kDisasm0,       // general disassembly view memory (K slots)

    kMemoryView0 = kDisasm0 + kNumDisasmViews,   // general memory view memorys (K Slots)

    kGraphicsInspector = kMemoryView0 + kNumMemoryViews,    // gfx bitmap
    kGraphicsInspectorVideoRegs,                            // same as kVideo but synced with graphics inspector requests

    kMemorySlotCount
};

// Check if 2 memory ranges overlap
bool Overlaps(uint32_t addr1, uint32_t size1, uint32_t addr2, uint32_t size);

// A block of memory pulled from the target.
struct Memory
{
public:
    Memory(uint32_t addr, uint32_t size);

    ~Memory();

    void Clear();

    void Set(uint32_t offset, uint8_t val)
    {
        assert(offset < m_size);
        m_pData[offset] = val;
    }

    uint8_t Get(uint32_t offset) const
    {
        assert(offset < m_size);
        return m_pData[offset];
    }

    uint32_t GetAddress() const
    {
        return m_addr;
    }

    bool HasAddress(uint32_t address) const
    {
        uint32_t offset = address - m_addr;
        return offset < m_size;
    }

    uint8_t ReadAddressByte(uint32_t address) const
    {
        uint32_t offset = address - m_addr;
        assert(offset < m_size);
        return Get(offset);
    }

    uint32_t GetSize() const
    {
        return m_size;
    }

    const uint8_t* GetData() const
    {
        return m_pData;
    }

    Memory& operator=(const Memory& other);

private:
    Memory(const Memory& other);	// hide to prevent accidental use

    uint32_t	m_addr;
    uint32_t	m_size;		// size in bytes
    uint8_t*	m_pData;
};

#endif // MEMORY_H
