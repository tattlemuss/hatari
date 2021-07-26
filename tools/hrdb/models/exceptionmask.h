#ifndef EXCEPTIONMASK_H
#define EXCEPTIONMASK_H
#include <stdint.h>

class ExceptionMask
{
public:
    enum Type
    {
        kBus = 0,
        kAddress = 1,
        kIllegal = 2,
        kZeroDiv = 3,
        kChk = 4,
        kTrapv = 5,
        kPrivilege = 6,
        kTrace = 7,
        kExceptionCount = 8
    };

    // Exception numbers in EX "register"
#if 0
{ EXCEPT_BUS,       "Bus error" },              /* 2 */
{ EXCEPT_ADDRESS,   "Address error" },          /* 3 */
{ EXCEPT_ILLEGAL,   "Illegal instruction" },	/* 4 */
{ EXCEPT_ZERODIV,   "Div by zero" },		/* 5 */
{ EXCEPT_CHK,       "CHK" },			/* 6 */
{ EXCEPT_TRAPV,     "TRAPV" },			/* 7 */
{ EXCEPT_PRIVILEGE, "Privilege violation" },	/* 8 */
{ EXCEPT_TRACE,     "Trace" }			/* 9 */
    #endif

    ExceptionMask();

    bool Get(Type exceptionType) const
    {
        return (m_mask & (1U << exceptionType)) != 0;
    }

    void Set(Type exceptionType)
    {
        m_mask |= (1U << exceptionType);
    }

    uint16_t m_mask;

    static const char* GetName(uint32_t exceptionId);
};

#endif // EXCEPTIONMASK_H
