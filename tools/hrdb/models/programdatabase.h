#ifndef PROGRAMDATABASE_H
#define PROGRAMDATABASE_H

#include <map>
#include <QObject>
#include "../fonda/readelf.h"

// Represents debugging metadata for the active target program.
class ProgramDatabase : public QObject
{
    Q_OBJECT
public:
    explicit ProgramDatabase(QObject *parent = nullptr);

    bool SetPath(std::string path);
    void Clear();

    // TODO: is it best to convert to a base format?
    const std::vector<fonda::compilation_unit>& GetFileInfo() const
    {
        return m_elfInfo.line_info_units;
    }
    const QString& GetElfPath() const
    {
        return m_elfPath;
    }

    struct CodeInfo
    {
        uint32_t m_address;
        std::string m_dir;
        std::string m_file;
        int m_line;
        int m_column;
    };

    bool FindAddress(uint32_t address, ProgramDatabase::CodeInfo& codePoint) const;
    bool FindLowerOrEqual(uint32_t address, ProgramDatabase::CodeInfo& codePoint) const;
signals:

private:
    bool TryLoadElf(std::string path);

    // Path of elf/prg used to generate the data from.
    QString m_elfPath;

    // How to cross-reference a codepoint in fonda::elf_results
    struct DwarfLookup
    {
        uint32_t m_address;       // currently, address relative to text (might change)
        size_t m_compUnitIndex;   // index in elf_results::line_info_units
        size_t m_cpIndex;         // index in compilation_unit::points
    };

    // Maps a known address to a CodePoint
    typedef std::map<uint32_t, DwarfLookup> DwarfMap;
    DwarfMap m_addressMap;
    // Sorted key lookup
    std::vector<uint32_t> m_addrKeys;

    fonda::elf_results m_elfInfo;
};

#endif // PROGRAMDATABASE_H
