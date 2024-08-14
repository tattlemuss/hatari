#include "stringformat.h"
#include "memory.h"

QString Format::to_address(int memorySpace, uint32_t val)
{
    switch (memorySpace)
    {
    case Memory::kCpu:
        return QString::asprintf("$%x", val);
    case Memory::kDspP:
        return QString::asprintf("P:$%04x", val);
    case Memory::kDspX:
        return QString::asprintf("X:$%04x", val);
    case Memory::kDspY:
        return QString::asprintf("Y:$%04x", val);
    }
    return QString("?");
}
