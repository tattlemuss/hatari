#include "programdatabase.h"

#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include "../fonda/readelf.h"

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
    // Find potential matches
    QFileInfo info(QString::fromStdString(path));
    if (info.exists())
    {
        // Try .prg load here

    }

    QString baseName = info.completeBaseName();


    const char** pCurrExt = g_elfExtensions;
    while (*pCurrExt)
    {
        QString elfPath = baseName + QString::fromStdString(*pCurrExt);
        QFileInfo elfInfo(info.dir(), elfPath);
        qDebug() << elfInfo.filePath();
        qDebug() << info.dir().path() << baseName;
        if (elfInfo.exists())
        {
            // Try .elf load here
            bool loaded = TryLoadElf(elfInfo.filePath().toStdString());
            if (loaded)
                return true;
        }
        ++pCurrExt;
    }
    return false;
}

void ProgramDatabase::Clear()
{

}

bool ProgramDatabase::TryLoadElf(std::string path)
{
    FILE* pFile = fopen(path.c_str(), "rb");
    if (!pFile)
        return false;

    fonda::elf_results results;
    int ret = fonda::process_elf_file(pFile, results);
    fclose(pFile);

    if (ret != 0)
       return false;

    // Convert to PDB here

    return true;
}
