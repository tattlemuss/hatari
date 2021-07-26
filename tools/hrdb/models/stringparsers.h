#ifndef STRINGPARSERS_H
#define STRINGPARSERS_H

#include <stdint.h>

class SymbolTable;
class Registers;

class StringParsers
{
public:
    // Convert a single hex char to a 0-15 value
    // returns false if char is invalid
    static bool ParseHexChar(char c, uint8_t &result);

    // Convert a (non-prefixed) hex value string (null-terminated, with no
    // leading "$") to a u32
    // returns false if any char is invalid
    static bool ParseHexString(const char *pText, uint32_t &result);

    // Convert an expression string (null-terminated) to a u32
    // returns false if could not parse
    static bool ParseExpression(const char *pText, uint32_t &result, const SymbolTable& syms, const Registers& regs);
};

#endif // STRINGPARSERS_H
