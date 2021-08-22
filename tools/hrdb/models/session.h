#ifndef SESSION_H
#define SESSION_H

#include <QObject>
#include <QFont>

class QTcpSocket;
class QTimer;
class QTemporaryFile;
class Dispatcher;
class TargetModel;

#define VERSION_STRING      "0.001 (20 Aug 2021)"

// Shared runtime data about the debugging session used by multiple UI components
// This data isn't persisted over runs (that is saved in Settings)
class Session : public QObject
{
    Q_OBJECT
public:

    // Settings shared across the app and stored centrally.
    class Settings
    {
    public:
        QFont       m_font;
        // GRAPHICS INSPECTOR
        bool        m_bSquarePixels;
    };

    // DRAWING LAYOUT OPTIONS
    // Add a 4-pixel offset to shift away from the focus rectangle
    static const int kWidgetBorderX = 6;
    static const int kWidgetBorderY = 4;

    // Standard functions
    Session();
    virtual ~Session();
    void Connect();
    void Disconnect();

    QTcpSocket*     m_pTcpSocket;
    QTemporaryFile* m_pStartupFile;
    QTemporaryFile* m_pLoggingFile;

    // Connection data
    Dispatcher*     m_pDispatcher;
    TargetModel*    m_pTargetModel;

    const Settings& GetSettings() const;
    // Apply settings in prefs dialog.
    // Also emits settingsChanged()
    void SetSettings(const Settings& newSettings);

    void loadSettings();
    void saveSettings();

signals:
    void settingsChanged();

private slots:

    // Called shortly after stop notification received
    void connectTimerCallback();
private:
    QTimer*          m_pTimer;
    bool             m_autoConnect;

    // Actual stored settings object
    Settings        m_settings;
};

#endif // SESSION_H
