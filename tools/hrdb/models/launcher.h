#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <QString>
#include <QStringList>

class QSettings;
class Session;

class LaunchSettings
{
public:
    void loadSettings(QSettings& settings);
    void saveSettings(QSettings& settings) const;

    // What sort of automatic breakpoint to use
    enum BreakMode
    {
        kNone,
        kBoot,
        kProgStart
    };

    int m_breakMode;                // one of BreakMode above
    QString m_hatariFilename;
    QString m_prgFilename;          // .prg or TOS file to launch
    QString m_workingDirectory;
    QString m_watcherFiles;
    QString m_argsTxt;
    bool m_watcherActive;
};

// Returns true on success (Qt doesn't offer more options?)
bool LaunchHatari(const LaunchSettings& settings, const Session* pSession);

#endif // LAUNCHER_H
