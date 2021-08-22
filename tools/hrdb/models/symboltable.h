#ifndef SYMBOLTABLE_H
#define SYMBOLTABLE_H

#include "stdint.h"
#include <string>
#include <vector>
#include <map>

struct Symbol
{
    std::string name;
    uint32_t address;
    uint32_t size;
    std::string type;
};

class SymbolSubTable
{
public:
    void Clear();

    void AddSymbol(std::string name, uint32_t address, uint32_t size, std::string type);

    // Set up internal cache structures
    void CreateCache();

    size_t Count() const { return m_symbols.size(); }
    bool Find(uint32_t address, Symbol& result) const;
    bool FindLowerOrEqual(uint32_t address, Symbol& result) const;
    bool Find(std::string name, Symbol& result) const;
    const Symbol Get(size_t index) const;

private:
    std::vector<Symbol> m_symbols;

    // Cache data for faster lookup
    typedef std::pair<uint32_t, size_t> Pair;
    typedef std::map<uint32_t, size_t> Map;
    typedef std::vector<Pair>   Keys;
    Map m_addrLookup;
    Keys m_addrKeys;    // sorted address keys
};

class SymbolTable
{
public:
    SymbolTable();

    void Reset();

    void SetHatariSubTable(const SymbolSubTable& subtable);
    const SymbolSubTable& GetHatariSubTable() const { return m_subTables[kHatari]; }

    size_t Count() const;
    bool Find(uint32_t address, Symbol& result) const;
    bool FindLowerOrEqual(uint32_t address, Symbol& result) const;
    bool Find(std::string name, Symbol& result) const;
    const Symbol Get(size_t index) const;

private:
    enum TableId
    {
        kHatari,
        kHardware,
        kNumTables
    };
    SymbolSubTable   m_subTables[kNumTables];
};

#endif // SYMBOLTABLE_H
