#include "stringparsers.h"
#include <vector>
#include <string.h>

#include "symboltable.h"
#include "registers.h"

//-----------------------------------------------------------------------------
static bool IsAlphaLower(char c)
{
    return c >= 'a' && c <= 'z';
}

//-----------------------------------------------------------------------------
static bool IsAlphaUpper(char c)
{
    return c >= 'A' && c <= 'Z';
}

//-----------------------------------------------------------------------------
bool StringParsers::IsAlpha(char c)
{
    return IsAlphaLower(c) || IsAlphaUpper(c);
}

//-----------------------------------------------------------------------------
bool StringParsers::ParseHexChar(char c, uint8_t& result)
{
    if (c >= '0' && c <= '9')
    {
        result = (uint8_t)(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f')
    {
        result = (uint8_t)(10 + c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'F')
    {
        result = (uint8_t)(10 + c - 'A');
        return true;
    }
    result = 0;
    return false;
}

//-----------------------------------------------------------------------------
bool StringParsers::ParseDecChar(char c, uint8_t& result)
{
    if (c >= '0' && c <= '9')
    {
        result = (uint8_t)(c - '0');
        return true;
    }
    result = 0;
    return false;
}

//-----------------------------------------------------------------------------
bool StringParsers::ParseHexString(const char *pText, uint32_t& result)
{
    result = 0;

    // Pre-check to see if the first char is hex
    uint8_t val;
    if (!ParseHexChar(*pText, val))
        return false;

    // This needs to be 9 to include the terminator!
    for (int i = 0; i < 9; ++i)
    {
        if (*pText == 0)
            return true;

        if (!ParseHexChar(*pText, val))
        {
            result = 0;
            return false;
        }
        result <<= 4;
        result |= val;
        ++pText;
    }
    // Too many characters
    return false;
}

//-----------------------------------------------------------------------------
static bool IsWhitespace(char c)
{
    return c == ' ' || c == '\t';
}

//-----------------------------------------------------------------------------
static bool IsDecimalDigit(char c)
{
    return c >= '0' && c <= '9';
}

//-----------------------------------------------------------------------------
static bool IsSymbolStart(char c)
{
    return StringParsers::IsAlpha(c) || c == '_';
}

//-----------------------------------------------------------------------------
static bool IsSymbolMain(char c)
{
    // Support "." for e.g. "D0.W"
    return StringParsers::IsAlpha(c) || IsDecimalDigit(c) || c == '_' || c == '.';
}

//-----------------------------------------------------------------------------
static bool IsRegisterName(const char* name, int& regId)
{
    for (int i = 0; i < Registers::REG_COUNT; ++i)
    {
#ifdef WIN32
        if (_stricmp(name, Registers::s_names[i]) != 0)
            continue;
#else
        if (strcasecmp(name, Registers::s_names[i]) != 0)
            continue;
#endif
        regId = i;
        return true;
    }
    regId = Registers::REG_COUNT;
    return false;
}

//-----------------------------------------------------------------------------
static bool IsRegisterNameDotW(const char* name, int& regId)
{
    static const char* names[] = { "D0.W", "D1.W" ,"D2.W", "D3.W",
                                   "D4.W", "D5.W" ,"D6.W", "D7.W" };

    for (int i = 0; i < 8; ++i)
    {
#ifdef WIN32
        if (_stricmp(name, names[i]) != 0)
            continue;
#else
        if (strcasecmp(name, names[i]) != 0)
            continue;
#endif
        regId = i + Registers::D0;
        return true;
    }
    regId = Registers::REG_COUNT;
    return false;
}

//-----------------------------------------------------------------------------
struct Token
{
    enum Type
    {
        CONSTANT,
        ADD,
        SUB,
        MUL,
        DIV,
        LEFT_BRACE,
        RIGHT_BRACE
    };

    uint64_t    val;        // for constants
    Type        type;
};

//-----------------------------------------------------------------------------
bool ApplyOp(uint64_t val1, uint64_t val2, Token::Type op, uint64_t& result)
{
    if (op == Token::ADD)
    {
        result = val1 + val2; return true;
    }
    if (op == Token::SUB)
    {
        result = val1 - val2; return true;
    }
    if (op == Token::MUL)
    {
        result = val1 * val2; return true;
    }
    if (op == Token::DIV)
    {
        if (val2 == 0)
            return false;
        result = val1 / val2; return true;
    }
    assert(0);
    return false;
}

//-----------------------------------------------------------------------------
// Bigger the number, higher the precedence
static int Precedence(Token::Type type)
{
    switch (type) {
    case Token::ADD: case Token::SUB:
        return 1;
    case Token::MUL: case Token::DIV:
        return 2;
    case Token::CONSTANT:
        assert(0);
        return 0;
    default:
        break;
    }
    // Braces?
    return 0;
}

//-----------------------------------------------------------------------------
// Straight port of https://www.geeksforgeeks.org/expression-evaluation/
bool Evaluate(std::vector<Token>& tokens, uint64_t& result)
{
    // stack to store integer values.
    std::vector<uint64_t> values;

    // stack to store operators.
    std::vector<Token::Type> ops;

    for(size_t i = 0; i < tokens.size(); i++)
    {
        // Current token is an opening
        // brace, push it to 'ops'
        if (tokens[i].type == Token::LEFT_BRACE)
        {
            ops.push_back(tokens[i].type);
        }
        else if (tokens[i].type == Token::CONSTANT)
        {
            values.push_back(tokens[i].val);
        }

        // Closing brace encountered, solve
        // entire brace.
        else if(tokens[i].type == Token::RIGHT_BRACE)
        {
            // Flush all the ops back to the matching left brace
            while(!ops.empty() && ops.back() != Token::LEFT_BRACE)
            {
                if (values.size() < 2)
                    return false;
                uint64_t val2 = values.back();
                values.pop_back();

                uint64_t val1 = values.back();
                values.pop_back();

                Token::Type op = ops.back();
                ops.pop_back();

                uint64_t res;
                if (!ApplyOp(val1, val2, op, res))
                    return false;
                values.push_back(res);
            }

            // pop opening brace.
            if(ops.size() == 0)
                return false;

            assert(ops.back() == Token::LEFT_BRACE);
            ops.pop_back();
        }
        // Current token is an operator.
        else
        {
            // While top of 'ops' has same or greater
            // precedence to current token, which
            // is an operator. Apply operator on top
            // of 'ops' to top two elements in values stack.
            // "operator" can include brace which has lower precs
            while(!ops.empty() &&
                  Precedence(ops.back()) >= Precedence(tokens[i].type))
            {
                if (values.size() < 2)
                    return false;
                uint64_t val2 = values.back();
                values.pop_back();

                uint64_t val1 = values.back();
                values.pop_back();

                Token::Type op = ops.back();
                ops.pop_back();

                uint64_t res;
                if (!ApplyOp(val1, val2, op, res))
                    return false;
                values.push_back(res);
            }

            // Push current token to 'ops'.
            ops.push_back(tokens[i].type);
        }
    }

    // Entire expression has been parsed at this
    // point, apply remaining ops to remaining
    // values.
    while(!ops.empty())
    {
        if (values.size() < 2)
            return false;

        uint64_t val2 = values.back();
        values.pop_back();

        uint64_t val1 = values.back();
        values.pop_back();

        Token::Type op = ops.back();
        ops.pop_back();

        uint64_t res;
        if (!ApplyOp(val1, val2, op, res))
            return false;
        values.push_back(res);
    }
    result = values.back();
    return true;
}

//-----------------------------------------------------------------------------
static void AddOperator(std::vector<Token>& tokens, Token::Type type)
{
    Token t;
    t.type = type;
    t.val = 0;
    tokens.push_back(t);
}

//-----------------------------------------------------------------------------
bool StringParsers::ParseCpuExpression(const char *pText, uint32_t &result, const SymbolTable& syms, const Registers& regs)
{
    std::vector<Token> tokens;
    while (*pText != 0)
    {
        char head = *pText++;
        // Decide on next token type
        if (IsWhitespace(head))
        {
            ++pText;
            continue;
        }

        if (head == '$')
        {
            // Hex constant
            Token t;
            t.type = Token::CONSTANT;
            t.val = 0;
            uint8_t ch;
            // First char must exist, else "$" is a valid expression!
            if (!ParseHexChar(*pText, ch))
                return false;

            int numChars = 0;
            while (ParseHexChar(*pText, ch))
            {
                t.val <<= 4;
                t.val |= ch;
                ++pText;
                ++numChars;
                if (numChars > 16)   // overflow
                    return false;
            }
            tokens.push_back(t);
            continue;
        }
        else if (IsDecimalDigit(head))
        {
            // Decimal constant
            Token t;
            t.type = Token::CONSTANT;
            t.val = 0;
            uint8_t ch;
            --pText;        // go back to the first char
            while (ParseDecChar(*pText, ch))
            {
                t.val *= 10;
                t.val += ch;
                ++pText;
            }
            tokens.push_back(t);
            continue;
        }
        else if (IsSymbolStart(head))
        {
            // Possible symbol
            std::string name;
            name += head;
            while (IsSymbolMain(*pText))
                name += *pText++;

            // This might be a register name, use in preference to symbols
            int regId = 0;
            if (IsRegisterName(name.c_str(), regId))
            {
                Token t;
                t.type = Token::CONSTANT;
                t.val = regs.m_value[regId];
                tokens.push_back(t);
                continue;
            }

            if (IsRegisterNameDotW(name.c_str(), regId))
            {
                // Sign-extended Data registers.
                Token t;
                t.type = Token::CONSTANT;
                t.val = regs.m_value[regId] & 0xffff;
                if (t.val & 0x8000)
                    t.val |= 0xffff0000;     // extend to full EA

                // Sign extend
                tokens.push_back(t);
                continue;
            }

            // Try to look up symbols by name
            Symbol s;
            if (syms.Find(name, s))
            {
                Token t;
                t.type = Token::CONSTANT;
                t.val = s.address;
                tokens.push_back(t);
                continue;
            }
        }
        else if (head == '+')
        {
            AddOperator(tokens, Token::ADD);
            continue;
        }
        else if (head == '-')
        {
            AddOperator(tokens, Token::SUB);
            continue;
        }
        else if (head == '*')
        {
            AddOperator(tokens, Token::MUL);
            continue;
        }
        else if (head == '/')
        {
            AddOperator(tokens, Token::DIV);
            continue;
        }
        else if (head == '(')
        {
            AddOperator(tokens, Token::LEFT_BRACE);
            continue;
        }
        else if (head == ')')
        {
            AddOperator(tokens, Token::RIGHT_BRACE);
            continue;
        }
        // Couldn't match
        return false;
    }

    if (tokens.size() != 0)
    {
        uint64_t res2;
        bool success = Evaluate(tokens, res2);
        if (success)
        {
            result = (uint32_t)res2;
            return success;
        }
    }
    return false;
}

bool StringParsers::ParseMemAddrExpression(const char *pText, MemAddr &result, const SymbolTable &syms, const Registers &regs)
{
    result.space = MEM_CPU;
    result.addr = 0;
    int len = strlen(pText);
    if (len >= 2)
    {
        // Check for DSP memory spaces using "p:" etc
        if (pText[1] == ':')
        {
            switch (pText[0])
            {
            case 'P':
            case 'p':
                result.space = MEM_P;
                pText += 2;
                return ParseCpuExpression(pText, result.addr, syms, regs);
            case 'X':
            case 'x':
                result.space = MEM_X;
                pText += 2;
                return ParseCpuExpression(pText, result.addr, syms, regs);
            case 'Y':
            case 'y':
                result.space = MEM_Y;
                pText += 2;
                return ParseCpuExpression(pText, result.addr, syms, regs);
            }

        }
    }
    return ParseCpuExpression(pText, result.addr, syms, regs);
}

bool StringParsers::ParseHexString(const char *pText, QVector<uint8_t>& result)
{
    result.clear();

    // Pre-check to see if the first char is hex
    uint8_t val;
    if (!ParseHexChar(*pText, val))
        return false;

    while (*pText != 0)
    {
        uint8_t accum = 0;
        uint8_t val = 0;
        if (!ParseHexChar(*pText, val))
            return false;

        accum = val;

        ++pText;
        if (!ParseHexChar(*pText, val))
            return false;
        accum <<= 4;
        accum |= val;
        ++pText;
        result.push_back(accum);
    }
    return true;
}
