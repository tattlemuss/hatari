#include "exceptionmask.h"

// Exception numbers in EX "register"
// ... but in log.h:
#define	EXCEPT_NOHANDLER (1<<0)
#define	EXCEPT_BUS       (1<<1)
#define	EXCEPT_ADDRESS   (1<<2)
#define	EXCEPT_ILLEGAL   (1<<3)
#define	EXCEPT_ZERODIV   (1<<4)
#define	EXCEPT_CHK       (1<<5)
#define	EXCEPT_TRAPV     (1<<6)
#define	EXCEPT_PRIVILEGE (1<<7)
#define	EXCEPT_TRACE     (1<<8)
#define	EXCEPT_LINEA     (1<<9)
#define	EXCEPT_LINEF     (1<<10)
#define EXCEPT_DSP       (1<<30)

static uint32_t emToHatariBits[] =
{
    EXCEPT_BUS,
    EXCEPT_ADDRESS,
    EXCEPT_ILLEGAL,
    EXCEPT_ZERODIV,
    EXCEPT_CHK,
    EXCEPT_TRAPV,
    EXCEPT_PRIVILEGE,
    EXCEPT_TRACE,
    EXCEPT_LINEA,
    EXCEPT_LINEF,
    EXCEPT_DSP,
};

static_assert(sizeof(emToHatariBits) == ExceptionMask::kExceptionCount * sizeof(uint32_t));

ExceptionMask::ExceptionMask()
{
    m_mask = 0;
}

void ExceptionMask::SetFromHatari(uint32_t hatariMask)
{
    uint32_t m = 0;
    for (uint32_t i = 0; i < kExceptionCount; ++i)
    {
        if ((hatariMask & emToHatariBits[i]) != 0)
            m |= (1 << i);
    }
    this->m_mask = m;
}

uint32_t ExceptionMask::GetAsHatari() const
{
    uint32_t m = 0;
    for (uint32_t i = 0; i < kExceptionCount; ++i)
    {
        if ((m_mask & (1U << i)) != 0)
            m |= emToHatariBits[i];
    }
    return m;
}

bool ExceptionMask::Get(Type exceptionType) const
{
    return (m_mask & (1U << (exceptionType))) != 0;
}

void ExceptionMask::Set(Type exceptionType)
{
    m_mask |= (1U << (exceptionType));
}

const char *ExceptionMask::GetName(Type exceptionId)
{
    switch (exceptionId)
    {
    case kBus: return "Bus error (2)";
    case kAddress: return "Address error (3)";
    case kIllegal: return "Illegal instruction (4)";
    case kZeroDiv: return "Div by zero (5)";
    case kChk: return "CHK (6)";
    case kTrapv: return "TRAPV (7)";
    case kPrivilege: return "Privilege violation (8)";
    case kTrace: return "Trace";
    case kLineA: return "Line-A";
    case kLineF: return "Line-F";
    case kDsp: return "DSP Exception";
    default: break;
    }
    return "Unknown";
}

const char* ExceptionMask::GetExceptionVectorName(uint32_t exceptionVec)
{
    // The value in the Exception register is the vector number
    // e.g. 2 for Bus Error.
    switch (exceptionVec)
    {
    case 2: return "Bus error";
    case 3: return "Address error";
    case 4: return "Illegal instruction";
    case 5: return "Div by zero";
    case 6: return "CHK";
    case 7: return "TRAPV";
    case 8: return "Privilege violation";
    case 9: return "Trace";
    case 10: return "Line-A";
    case 15: return "Line-F";
    default: break;
    }
    return "Unknown";
}
