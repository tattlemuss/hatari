#include "symboltext.h"

QString DescribeSymbol(const SymbolTable& table, uint32_t addr)
{
    Symbol sym;
    if (!table.FindLowerOrEqual(addr & 0xffffff, true, sym))
        return QString();

    uint32_t offset = addr - sym.address;
    if (offset)
        return QString::asprintf("%s+$%x", sym.name.c_str(), offset);
    return QString::fromStdString(sym.name);
}

QString DescribeSymbolComment(const SymbolTable& table, uint32_t addr)
{
    Symbol sym;
    if (!table.FindLowerOrEqual(addr & 0xffffff, true, sym))
        return QString();

    return QString::fromStdString(sym.comment);
}
