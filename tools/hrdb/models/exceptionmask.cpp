#include "exceptionmask.h"

ExceptionMask::ExceptionMask()
{
    m_mask = 0;
}

const char *ExceptionMask::GetName(uint32_t exceptionId)
{
    switch (exceptionId)
    {
    case 2: return "Bus error";
    case 3: return "Address error";
    case 4: return "Illegal instruction";
    case 5: return "Div by zero";
    case 6: return "CHK";
    case 7: return "TRAPV";
    case 8: return "Privilege violation";
    case 9: return "Trace";
    default: break;
    }
    return "Unknown";
}
