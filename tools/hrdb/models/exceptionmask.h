#ifndef EXCEPTIONMASK_H
#define EXCEPTIONMASK_H
#include <stdint.h>

/*
 * Abstract represetation of the set of enabled exceptions.
 *
 * NOTE: our representation of the exception mask is independent of
 * those (multiple) representations in Hatari. We convert back and
 * forth from that representation as required.
 */
class ExceptionMask
{
public:
    enum Type : uint32_t
    {
        kBus = 0,
        kAddress = 1,
        kIllegal = 2,
        kZeroDiv = 3,
        kChk = 4,
        kTrapv = 5,
        kPrivilege = 6,
        kTrace = 7,
        kLineA = 8,
        kLineF = 9,
        kDsp = 10,
        kExceptionCount = 11
    };

    ExceptionMask();

    // Convert the Hatari raw exmask value to our internal format.
    void SetFromHatari(uint32_t hatariMask);
    // Generate an exmask value that Hatari likes.
    uint32_t GetAsHatari() const;

    // Return whether we want the exception type tracked.
    bool Get(Type exceptionType) const;

    // Flag an internal bit that we want the exception type tracked.
    void Set(Type exceptionType);

    // Get name string from internal enum.
    static const char* GetName(Type exceptionId);

    // Get name string from the vector number supplied in the "EX" pseudo-register.
    static const char* GetExceptionVectorName(uint32_t exceptionVec);
private:
    uint32_t m_mask;        // Accumulated mask value from set Types
};

#endif // EXCEPTIONMASK_H
