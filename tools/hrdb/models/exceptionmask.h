#ifndef EXCEPTIONMASK_H
#define EXCEPTIONMASK_H
#include <stdint.h>

class ExceptionMask
{
public:
    enum Type
    {
        kBus = 1,
        kAddress = 2,
        kIllegal = 3,
        kZeroDiv = 4,
        kChk = 5,
        kTrapv = 6,
        kPrivilege = 7,
        kTrace = 8,
        kExceptionCount = 9
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

// ... but in log.h:
#define	EXCEPT_NOHANDLER (1<<0)
#define	EXCEPT_BUS	 (1<<1)
#define	EXCEPT_ADDRESS 	 (1<<2)
#define	EXCEPT_ILLEGAL	 (1<<3)
#define	EXCEPT_ZERODIV	 (1<<4)
#define	EXCEPT_CHK	 (1<<5)
#define	EXCEPT_TRAPV	 (1<<6)
#define	EXCEPT_PRIVILEGE (1<<7)
#define	EXCEPT_TRACE     (1<<8)
#define	EXCEPT_LINEA     (1<<9)
#define	EXCEPT_LINEF     (1<<10)

    #endif

    ExceptionMask();

    bool Get(Type exceptionType) const
    {
        return (m_mask & (1U << (exceptionType))) != 0;
    }

    void Set(Type exceptionType)
    {
        m_mask |= (1U << (exceptionType));
    }

    uint16_t m_mask;

    static const char* GetName(uint32_t exceptionId);
};

#endif // EXCEPTIONMASK_H
