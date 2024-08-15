#ifndef STRINGPARSERS_H
#define STRINGPARSERS_H

#include <stdint.h>
#include <QVector>
#include "memaddr.h"

class SymbolTable;
class Registers;

// Utility functions to parse characters and character strings
class StringParsers
{
public:

    // Returns true if c is an ASCII alphanumeric character.
    static bool IsAlpha(char c);

    // Convert a single hex char to a 0-15 value.
    // Returns false if char is invalid.
    static bool ParseHexChar(char c, uint8_t &result);

    // Convert a single decimal char to a 0-9 value.
    // Returns false if char is invalid.
    static bool ParseDecChar(char c, uint8_t& result);

    // Convert a (non-prefixed) hex value string (null-terminated, with no
    // leading "$") to a u32.
    // Returns false if any char is invalid.
    // Will be subject to overflow if the string is too big!
    static bool ParseHexString(const char *pText, uint32_t &result);

    // Convert a (non-prefixed) unlimited hex value string (null-terminated, with no
    // leading "$") to a vector of uint8_t values.
    static bool ParseHexString(const char *pText, QVector<uint8_t>& result);

    // Convert an expression string (null-terminated) to a u32.
    // Allows looking up register and symbol values.
    // Returns false if could not parse
    static bool ParseCpuExpression(const char *pText, uint32_t &result, const SymbolTable& syms, const Registers& regs);

    // Convert an address expression string (null-terminated) to a u32.
    // Allows looking up register and symbol values.
    // Returns false if could not parse
    static bool ParseMemAddrExpression(const char *pText, MemAddr& result, const SymbolTable& syms, const Registers& regs);
};

#endif // STRINGPARSERS_H
