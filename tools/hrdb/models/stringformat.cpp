#include "stringformat.h"
#include "memory.h"

QString Format::to_address(int memorySpace, uint32_t val)
{
    switch (memorySpace)
    {
    case MEM_CPU:
        return QString::asprintf("$%x", val);
    case MEM_P:
        return QString::asprintf("P:$%04x", val);
    case MEM_X:
        return QString::asprintf("X:$%04x", val);
    case MEM_Y:
        return QString::asprintf("Y:$%04x", val);
    }
    return QString("?");
}
