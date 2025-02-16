#include "breakpoint.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>

/* define which character indicates which type of number on expression  */
#define PREFIX_BIN '%'                            /* binary decimal     */
#define PREFIX_DEC '#'                             /* normal decimal    */
#define PREFIX_HEX '$'                             /* hexadecimal       */

/**
 * Parse & set an (unsigned) number, assuming it's in the configured
 * default number base unless it has a prefix:
 * - '$' / '0x' / '0h' => hexadecimal
 * - '#' / '0d' => normal decimal
 * - '%' / '0b' => binary decimal
 * - '0o' => octal decimal
 * Return how many characters were parsed or zero for error.
 */
static int getNumber(const char *str, uint32_t *number, int *nbase)
{
    char *end;
    const char *start = str;
    int base = 10; //ConfigureParams.Debugger.nNumberBase;
    unsigned long int value;

    if (!str[0]) {
        fprintf(stderr, "Value missing!\n");
        return 0;
    }

    /* determine correct number base */
    if (str[0] == '0') {

        /* 0x & 0h = hex, 0d = dec, 0o = oct, 0b = bin ? */
        switch(str[1]) {
        case 'b':
            base = 2;
            break;
        case 'o':
            base = 8;
            break;
        case 'd':
            base = 10;
            break;
        case 'h':
        case 'x':
            base = 16;
            break;
        default:
            str -= 2;
        }
        str += 2;
    }
    else if (!isxdigit((unsigned char)str[0])) {

        /* doesn't start with (hex) number -> is it prefix? */
        switch (*str++) {
        case PREFIX_BIN:
            base = 2;
            break;
        case PREFIX_DEC:
            base = 10;
            break;
        case PREFIX_HEX:
            base = 16;
            break;
        default:
            //fprintf(stderr, "Unrecognized number prefix in '%s'!\n", start);
            return 0;
        }
    }
    *nbase = base;

    /* parse number */
    errno = 0;
    value = strtoul(str, &end, base);
    if (errno == ERANGE /*&& value == LONG_MAX*/) {
        fprintf(stderr, "Overflow with value '%s'!\n", start);
        return 0;
    }
    if ((errno != 0 && value == 0) || end == str) {
        fprintf(stderr, "Invalid value '%s'!\n", start);
        return 0;
    }
    *number = value;
    return end - start;
}

/*
 *
 Usage:  b <condition> [&& <condition> ...] [:<option>] | <index> | help | all

  condition = <value>[.mode] [& <mask>] <comparison> <value>[.mode]

where:
        value = [(] <register/symbol/variable name | number> [)]
        number/mask = [#|$|%]<digits>
        comparison = '<' | '>' | '=' | '!'
        addressing mode (width) = 'b' | 'w' | 'l'
        addressing mode (space) = 'p' | 'x' | 'y'

        */

void Breakpoint::SetExpression(const std::string& exp)
{
    m_expression = exp;
    m_pcHack = 0;

    if (exp.find("pc = ") == 0)
    {
        uint32_t number;
        int base;
        if (getNumber(exp.c_str() + 5, &number, &base))
        {
            m_pcHack = number;
        }
    }
}
