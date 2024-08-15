#ifndef MEMADDR_H
#define MEMADDR_H

// Basic definitions for memory location.
// Names are terse since these are common.

// Memory locations that can be queried.
enum MemSpace
{
    MEM_CPU,
    MEM_P,
    MEM_X,
    MEM_Y
};

// Describes a unique memory address from the target.
struct MemAddr
{
    MemSpace    space;      // one of MemSpace
    uint32_t    addr;

    bool operator==(const MemAddr& other)
    {
        return space == other.space && addr == other.addr;
    }
    bool operator!=(const MemAddr& other)
    {
        return space != other.space || addr != other.addr;
    }
};

// Create a MemoryAddress
inline MemAddr maddr(MemSpace space, uint32_t addr)
{
    MemAddr m;
    m.space = space; m.addr = addr;
    return m;
}

#endif // MEMADDR_H
