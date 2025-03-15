#include "programdatabase.h"

#include <QFileInfo>
#include <QDir>
#include <QDebug>

static const char* g_elfExtensions[] = {
    ".elf",
    ".ELF",
    ".o",
    ".O",
    nullptr
};

ProgramDatabase::ProgramDatabase(QObject *parent)
    : QObject{parent}
{

}

bool ProgramDatabase::SetPath(std::string path)
{
    m_elfPath.clear();

    // Find potential matches
    QFileInfo info(QString::fromStdString(path));
    if (info.exists())
    {
        // Try .prg load here

    }

    QString baseName = info.completeBaseName();
    const char** pCurrExt = g_elfExtensions;
    bool found = false;
    while (*pCurrExt)
    {
        QString elfPath = baseName + QString::fromStdString(*pCurrExt);
        QFileInfo elfInfo(info.dir(), elfPath);
        if (elfInfo.exists())
        {
            // Try .elf load here
            bool loaded = TryLoadElf(elfInfo.filePath().toStdString());
            if (loaded)
            {
                m_elfPath = elfInfo.absoluteFilePath();
                qDebug() << "Elf path: " << m_elfPath;
                found = true;
                break;
            }
        }
        ++pCurrExt;
    }

    return found;
}

void ProgramDatabase::Clear()
{
    m_elfInfo.line_info_units.clear();
    m_elfInfo.sections.clear();
    m_elfInfo.symbols.clear();
    m_addrKeys.clear();
    m_addressMap.clear();
}

bool ProgramDatabase::FindAddress(uint32_t address, ProgramDatabase::CodeInfo& codePoint) const
{
    const DwarfMap::const_iterator it = m_addressMap.find(address);
    if (it == m_addressMap.end())
        return false;

    const fonda::compilation_unit& unit = m_elfInfo.line_info_units[it->second.m_compUnitIndex];
    const fonda::code_point& dwarfCp = unit.points[it->second.m_cpIndex];
    const fonda::compilation_unit::file& dwarfFile = unit.files[dwarfCp.file_index];

    codePoint.m_address = address;
    codePoint.m_dir = unit.dirs[dwarfFile.dir_index];
    codePoint.m_file = dwarfFile.path;
    codePoint.m_column = dwarfCp.column;
    codePoint.m_line = dwarfCp.line;
    return true;
}

bool ProgramDatabase::FindLowerOrEqual(uint32_t address, ProgramDatabase::CodeInfo& codePoint) const
{
    // Degenerate cases
    if (m_addrKeys.size() == 0)
        return false;

    //if (address < m_symbols[m_addrKeys[0].second].address)
    //    return false;

    // Binary chop
    size_t lower = 0;
    size_t upper_plus_one = m_addrKeys.size();

    // We need to find the first item which is *lower or equal* to N,
    while ((lower + 1) < upper_plus_one)
    {
        size_t mid = (lower + upper_plus_one) / 2;
        uint32_t midAddr = m_addrKeys[mid];
        if (midAddr <= address)
        {
            // mid is lower/equal, search
            lower = mid;
        }
        else {
            // mid is
            upper_plus_one = mid;
        }
    }

    uint32_t addr = m_addrKeys[lower];
    return FindAddress(addr, codePoint);
}

bool ProgramDatabase::TryLoadElf(std::string path)
{
    FILE* pFile = fopen(path.c_str(), "rb");
    if (!pFile)
        return false;

    int ret = fonda::process_elf_file(pFile, m_elfInfo);
    fclose(pFile);

    if (ret != 0)
       return false;

    // Generate our lookup information for the dwarf data
    // Run through all compilation units and record the addresses
    m_addressMap.clear();
    for (size_t cuIndex = 0; cuIndex < m_elfInfo.line_info_units.size(); ++cuIndex)
    {
        const fonda::compilation_unit& cu = m_elfInfo.line_info_units[cuIndex];
        for (size_t cpIndex = 0; cpIndex < cu.points.size(); ++cpIndex)
        {
            const fonda::code_point& pt = cu.points[cpIndex];
            DwarfLookup ourCp;
            ourCp.m_address = pt.address;
            ourCp.m_compUnitIndex = cuIndex;
            ourCp.m_cpIndex = cpIndex;

            m_addressMap.insert(std::make_pair(ourCp.m_address, ourCp));
        }
    }

    // Recalc the keys table
    m_addrKeys.clear();
    DwarfMap::iterator it(m_addressMap.begin());
    while (it != m_addressMap.end())
    {
        m_addrKeys.push_back(it->second.m_address);
        ++it;
    }

    return true;
}
