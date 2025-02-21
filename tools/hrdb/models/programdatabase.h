#ifndef PROGRAMDATABASE_H
#define PROGRAMDATABASE_H

#include <QObject>

// Represents debugging metadata for the active target program.
class ProgramDatabase : public QObject
{
    Q_OBJECT
public:
    explicit ProgramDatabase(QObject *parent = nullptr);

    bool SetPath(std::string path);
    void Clear();
signals:

private:
    bool TryLoadElf(std::string path);
};

#endif // PROGRAMDATABASE_H
