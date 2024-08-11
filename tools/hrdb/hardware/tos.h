#ifndef TOS_H
#define TOS_H

#include <stdint.h>
#include <QString>
class Memory;

namespace hopper68
{
    struct instruction;
}

const char* GetGemdosName(uint16_t id);
const char* GetBiosName(uint16_t id);
const char* GetXbiosName(uint16_t id);

QString GetTrapAnnotation(uint8_t trapNum, uint16_t callId);

// Inspect memory instructions to guess at a system call ID
QString GetTOSAnnotation(const Memory& mem, uint32_t address, const hopper68::instruction& inst);

#endif // TOS_H
