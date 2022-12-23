#ifndef CONSOLEWINDOW_H
#define CONSOLEWINDOW_H

#include <QDockWidget>
#include <QFile>
#include <QTextStream>
#include "../models/memory.h"

class TargetModel;
class Dispatcher;
class Session;
class QLabel;
class QLineEdit;
class QTextEdit;
class QFileSystemWatcher;

class ConsoleWindow : public QDockWidget
{
    Q_OBJECT
public:
    ConsoleWindow(QWidget *parent, Session* pSession);
    virtual ~ConsoleWindow();

    // Grab focus and point to the main widget
    void keyFocus();

    void loadSettings();
    void saveSettings();

private:
    void connectChanged();
    void settingsChanged();
    void textEditChanged();
    void fileChanged(const QString& filename);

    void deleteWatcher();
    QLineEdit*          m_pLineEdit;
    QTextEdit*          m_pTextArea;

    Session*            m_pSession;
    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;

    // Data to watch and read the temporary file output by Hatari
    QFileSystemWatcher* m_pWatcher;
    QFile               m_tempFile;
    QTextStream         m_tempFileTextStream;
};

#endif // CONSOLEWINDOW_H
