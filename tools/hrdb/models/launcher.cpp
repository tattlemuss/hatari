#include "launcher.h"
#include <QTemporaryFile>
#include <QTextStream>
#include <QProcess>
#include <QSettings>
#include <QFileSystemWatcher>
#include <QObject>

#include "session.h"
#include "filewatcher.h"

void LaunchSettings::loadSettings(QSettings& settings)
{
    // Save launcher settings
    settings.beginGroup("LaunchSettings");
    m_hatariFilename = settings.value("exe", QVariant("hatari")).toString();
    m_argsTxt = settings.value("args", QVariant("")).toString();
    m_prgFilename = settings.value("prg", QVariant("")).toString();
    m_workingDirectory = settings.value("workingDirectory", QVariant("")).toString();
    m_hatariConfigFilename = settings.value("hatariConfigFilename", QVariant("")).toString();
    m_watcherFiles = settings.value("watcherFiles", QVariant("")).toString();
    m_watcherActive = settings.value("watcherActive", QVariant("false")).toBool();
    m_breakMode = settings.value("breakMode", QVariant("0")).toInt();
    m_fastLaunch = settings.value("fastLaunch", QVariant("false")).toBool();
    m_breakPointTxt = settings.value("breakPointTxt", QVariant("")).toString();

    m_exceptionMask.SetRaw(settings.value("autostartException", QVariant(0)).toUInt());
    settings.endGroup();
}

void LaunchSettings::saveSettings(QSettings &settings) const
{
    settings.beginGroup("LaunchSettings");
    settings.setValue("exe", m_hatariFilename);
    settings.setValue("args", m_argsTxt);
    settings.setValue("prg", m_prgFilename);
    settings.setValue("workingDirectory", m_workingDirectory);
    settings.setValue("hatariConfigFilename", m_hatariConfigFilename);
    settings.setValue("watcherFiles", m_watcherFiles);
    settings.setValue("watcherActive", m_watcherActive);
    settings.setValue("breakMode", m_breakMode);
    settings.setValue("fastLaunch", m_fastLaunch);
    settings.setValue("breakPointTxt", m_breakPointTxt);
    settings.setValue("autostartException", m_exceptionMask.GetRaw());
    settings.endGroup();
}

bool LaunchHatari(const LaunchSettings& settings, Session* pSession)
{
    // Create a copy of the args that we can adjust
    QStringList args;

    QString otherArgsText = settings.m_argsTxt;
    otherArgsText = otherArgsText.trimmed();
    if (otherArgsText.size() != 0)
        args = otherArgsText.split(" ");

    QString configFilename = settings.m_hatariConfigFilename.trimmed();
    if (configFilename.size() != 0)
    {
        // Prepend the "--configfile N" part (backwards!)
        args.push_front(configFilename);
        args.push_front("--configfile");
    }

    if (settings.m_watcherActive)
    {
        FileWatcher* pFileWatcher=((Session*)pSession)->createFileWatcherInstance();
        if (pFileWatcher)
        {
            pFileWatcher->clear(); //remove all watched files
            if(settings.m_watcherFiles.length()>0)
                pFileWatcher->addPaths(settings.m_watcherFiles.split(","));
            else
                pFileWatcher->addPath(settings.m_prgFilename);
        }
    }

    // First make a temp file for breakpoints etc

    {
        // Program start script file
        {
            QString tmpContents;
            QTextStream ref(&tmpContents);

            if (settings.m_fastLaunch)
                ref << "setopt --fast-forward 0\r\n";   // speed = 0 in bp file

            ref << "symbols prg\r\n";
            if (settings.m_breakMode == LaunchSettings::kProgramBreakpoint)
                ref << "b " << settings.m_breakPointTxt << ":once\r\n";

            // Create the temp file
            QTemporaryFile& tmp(*pSession->m_pProgramStartScript);
            if (tmp.exists())
                tmp.remove();
            if (!tmp.open())
                return false;

            tmp.setTextModeEnabled(true);
            tmp.write(tmpContents.toUtf8());
            tmp.close();
        }

        // Startup script file
        {
            QString tmpContents;
            QTextStream ref(&tmpContents);
            QString progStartFilename = pSession->m_pProgramStartScript->fileName();

            if (settings.m_fastLaunch)
                ref << "setopt --fast-forward 1\r\n";   // speed=1 in startup file

            // Generate some commands for
            // Break at boot/start commands
            if (settings.m_breakMode == LaunchSettings::BreakMode::kBoot)
                ref << "b pc ! 0 : once\r\n";     // don't run the breakpoint file yet

            if (settings.m_breakMode == LaunchSettings::BreakMode::kProgStart)
            {
                // Break at program start and run the program start script
                ref << "b pc=TEXT && pc<$e00000 :once :file " << progStartFilename << "\r\n";
            }
            else if (settings.m_fastLaunch ||
                     settings.m_breakMode == LaunchSettings::BreakMode::kProgramBreakpoint)
            {
               // run bp file but don't stop
                ref << "b pc=TEXT && pc<$e00000 :trace :once :file " << progStartFilename << "\r\n";
            }

            // Create the temp file
            // In theory we need to be careful about reuse?
            QTemporaryFile& tmp(*pSession->m_pStartupFile);
            if (tmp.exists())
                tmp.remove();

            if (!tmp.open())
                return false;

            tmp.setTextModeEnabled(true);
            tmp.write(tmpContents.toUtf8());
            tmp.close();

            // Prepend the "--parse N" part (backwards!)
            args.push_front(tmp.fileName());
            args.push_front("--parse");
        }
    }

    // If there are autostart exceptions, generate the string
    if (settings.m_exceptionMask.GetRaw() != 0)
    {
        QString autoStartStr;
        QTextStream ref(&autoStartStr);
        bool first = true;
        for (uint32_t i = 0; i < ExceptionMask::kExceptionCount; ++i)
        {
            ExceptionMask::Type t = (ExceptionMask::Type)i;
            if (settings.m_exceptionMask.Get(t))
            {
                if (!first)
                    ref << ",";
                ref << QString(ExceptionMask::GetAutostartArg(t));
                first = false;
            }
        }
        ref << ",autostart";
        args.push_back("--debug-except");
        args.push_back(autoStartStr);
    }

    // Executable goes as last arg
    args.push_back(settings.m_prgFilename);

    // Actually launch the program

    DetachableProcess* pNewProc = new DetachableProcess();

    // This needs to be done straight away so that the old process can be killed
    // and release any active network connection.
    pSession->setHatariProcess(pNewProc);

    pNewProc->setProgram(settings.m_hatariFilename);
    pNewProc->setArguments(args);

    // Redirect outputs to NULL so that Hatari's own spew doesn't cause lockups
    // if hrdb is killed and restarted (temp file contention?)
    pNewProc->setStandardOutputFile(QProcess::nullDevice());
    pNewProc->setStandardErrorFile(QProcess::nullDevice());
    pNewProc->setWorkingDirectory(settings.m_workingDirectory);

    // We now always start the process as attached.
    // We have a (rather iffy) QDetachableProcess and call detach() when
    // hrdb itself exits.
    // The
    pNewProc->start();
    return pNewProc->waitForStarted();
}
