#ifndef SESSION_H
#define SESSION_H

#include <QObject>
#include <QFont>
#include <QProcess>
#include "launcher.h"

class QTcpSocket;
class QTimer;
class QTemporaryFile;
class QFileSystemWatcher;
class Dispatcher;
class TargetModel;
class FileWatcher;
class ProgramDatabase;

#define VERSION_STRING      "0.009-DSP (August 2024)"
#define HELP_URL            "http://clarets.org/steve/projects/hrdb.html"

// Wrapper of QProcess that "allows" a detact() after start().
// This workaround class is from
// https://stackoverflow.com/questions/17501642/detaching-a-started-process/36562936#36562936
// It exploits the protected access to setProcessState() to prevent auto-termination
// of a process.
// I don't really like it, but it seems to provide the only way to do this.
class DetachableProcess : public QProcess
{
public:
    DetachableProcess(QObject *parent = 0) : QProcess(parent){}
    void detach()
    {
        this->waitForStarted();
        setProcessState(QProcess::NotRunning);
    }
};

// Shared runtime data about the debugging session used by multiple UI components
// This data isn't persisted over runs (that is saved in Settings)
class Session : public QObject
{
    Q_OBJECT
public:
    // What kind of window we want to pop up via requestAddress
    enum WindowType
    {
        kDisasmWindow,
        kMemoryWindow,
        kGraphicsInspector,
        kSourceWindow
    };

    // Settings shared across the app and stored centrally.
    class Settings
    {
    public:
        QFont       m_font;
        // GRAPHICS INSPECTOR
        bool        m_bSquarePixels;
        // DISASSEMBLY
        bool        m_bDisassHexNumerics;
        //
        enum ProfileDisplayMode
        {
            kTotal,
            kMean
        };

        int         m_profileDisplayMode;

        // LIVE UPDATE
        bool        m_liveRefresh;

        // Source association
        static const int kNumSearchDirectories = 4;
        QString     m_sourceSearchDirectories[kNumSearchDirectories];

        int         m_sourceTabSize;
    };

    // DRAWING LAYOUT OPTIONS
    // Add a 4-pixel offset to shift away from the focus rectangle
    static const int kWidgetBorderX = 6;
    static const int kWidgetBorderY = 4;

    // Standard functions
    Session();
    virtual ~Session() override;
    void Connect();
    void Disconnect();

    QTcpSocket*     m_pTcpSocket;
    QTemporaryFile* m_pStartupFile;             // Debugger commands at Hatari launch
    QTemporaryFile* m_pProgramStartScript;      // Debugger commands run at program start

    QTemporaryFile* m_pLoggingFile;
    FileWatcher*    m_pFileWatcher;

    // Connection data
    Dispatcher*     m_pDispatcher;
    TargetModel*    m_pTargetModel;

    // Controller Hatari process
    DetachableProcess*       m_pHatariProcess;

    // Line/symbol information
    ProgramDatabase*    m_pProgramDatabase;

    const Settings& GetSettings() const;
    const LaunchSettings& GetLaunchSettings() const;

    // Apply settings in prefs dialog.
    // Also emits settingsChanged()
    void SetSettings(const Settings& newSettings);
    void SetLaunchSettings(const LaunchSettings& newSettings);

    // Status bar/log
    void SetMessage(const QString& msg);

    void loadSettings();
    void saveSettings();

    void resetWarm();
    void resetCold();

    FileWatcher* createFileWatcherInstance();

    void setHatariProcess(DetachableProcess* pProc);

signals:
    void settingsChanged();

    // Shared signal to request a new address in another window.
    // Qt seems to have no central message dispatch, so use signals/slots
    void addressRequested(WindowType windowType, int windowId, int memSpace, uint32_t address);

    // Called by UI elements to flag a shared message
    void messageSet(const QString& msg);

    // Flagged when new debug data is loaded.
    void programDatabaseChanged();

private slots:

    // Called shortly after stop notification received
    void connectTimerCallback();
private:

    void programPathChanged();

    QTimer*          m_pTimer;
    bool             m_autoConnect;

    // Actual stored settings object
    Settings         m_settings;
    LaunchSettings   m_launchSettings;
};

#endif // SESSION_H
