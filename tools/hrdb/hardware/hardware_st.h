#ifndef HARDWARE_ST_H
#define HARDWARE_ST_H

#include <cstdint>

namespace HardwareST
{
static const uint32_t VIDEO_REGS_BASE    = 0xff8200;
static const uint32_t VIDEO_BASE_HI      = 0xff8201;
static const uint32_t VIDEO_BASE_MED     = 0xff8203;
static const uint32_t VIDEO_BASE_LO      = 0xff820d;        // valid for STE only
static const uint32_t VIDEO_PALETTE_0    = 0xff8240;
static const uint32_t VIDEO_RESOLUTION   = 0xff8260;
}

#endif // HARDWARE_ST_H
