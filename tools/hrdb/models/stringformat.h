#ifndef STRINGFORMAT_H
#define STRINGFORMAT_H

#include <stdint.h>
#include <QString>
#include <QTextStream>

struct MemAddr;

namespace Format
{
    // Format a value as "$xx" in a consistent fashion
    QString to_address(int memorySpace, uint32_t val);
    // Format a value as "$xx" in a consistent fashion
    QString to_address(const MemAddr& addr);

    // Format a value as "$xx" in a consistent fashion
    inline QString to_hex32(uint32_t val);

    // Format a 16-bit Absolute Address word to e.g.
    // "$80.w" or "$ffff8800.w" as appropriate.
    inline QString to_abs_word(uint16_t val);

    // Format a value as signed decimal or hexadecimal.
    // Handles the nasty "-$5" case
    inline QString to_signed(int32_t val, bool isHex);

    inline QTextStream &flush(QTextStream &stream);

    // This is a wrapper for Qt::endl() which isn't available
    // in Qt 5.12, but TextStream::endl() causes deprecation warnings.
    // We don't need to flush either.
    inline QTextStream &endl(QTextStream &stream);
};

// ----------------------------------------------------------------------------
//	INSTRUCTION DISPLAY FORMATTING
// ----------------------------------------------------------------------------
QString Format::to_hex32(uint32_t val)
{
    QString tmp;
    tmp = QString::asprintf("$%x", val);
    return tmp;
}

QString Format::to_abs_word(uint16_t val)
{
    QString tmp;
    if (val & 0x8000)
        tmp = QString::asprintf("$ffff%04x", val);
    else {
        tmp = QString::asprintf("$%x", val);
    }
    return tmp;
}

QString Format::to_signed(int32_t val, bool isHex)
{
    QString tmp;
    if (isHex)
    {
        if (val >= 0)
            return QString::asprintf("$%x", val);
        else
            return QString::asprintf("-$%x", -val);
    }
    else
    {
        return QString::asprintf("%d", val);
    }
}

QTextStream &Format::flush(QTextStream &stream)
{
    stream.flush();
    return stream;
}

QTextStream &Format::endl(QTextStream &stream)
{
    return stream << QLatin1Char('\n') << Format::flush;
}

#endif // STRINGFORMAT_H
