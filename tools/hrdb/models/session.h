#ifndef SESSION_H
#define SESSION_H

#include <QObject>
#include <QFont>
#include "launcher.h"

class QTcpSocket;
class QTimer;
class QTemporaryFile;
class QFileSystemWatcher;
class Dispatcher;
class TargetModel;
class FileWatcher;

#define VERSION_STRING      "0.007 (August 2022)"

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
        kGraphicsInspector
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

        // LIVE UPDATE
        bool        m_liveRefresh;
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
    QTemporaryFile* m_pStartupFile;
    QTemporaryFile* m_pLoggingFile;
    FileWatcher*    m_pFileWatcher;

    // Connection data
    Dispatcher*     m_pDispatcher;
    TargetModel*    m_pTargetModel;

    const Settings& GetSettings() const;
    const LaunchSettings& GetLaunchSettings() const;

    // Apply settings in prefs dialog.
    // Also emits settingsChanged()
    void SetSettings(const Settings& newSettings);
    void SetLaunchSettings(const LaunchSettings& newSettings);

    void loadSettings();
    void saveSettings();

    void resetEmulator();

    FileWatcher* createFileWatcherInstance();

signals:
    void settingsChanged();

    // Shared signal to request a new address in another window.
    // Qt seems to have no central message dispatch, so use signals/slots
    void addressRequested(WindowType windowType, int windowId, uint32_t address);

    // Called by the main window when all data such as regs, breakpoints, symbols
    // are updated.
    void mainStateUpdated();
private slots:

    // Called shortly after stop notification received
    void connectTimerCallback();
private:
    QTimer*          m_pTimer;
    bool             m_autoConnect;

    // Actual stored settings object
    Settings         m_settings;
    LaunchSettings   m_launchSettings;
};

#endif // SESSION_H
