#ifndef SOURCEWINDOW_H
#define SOURCEWINDOW_H

#include <QDockWidget>
#include "showaddressactions.h"

class TargetModel;
class Dispatcher;
class Session;
class QLabel;
class QTextEdit;
class QComboBox;

class SourceCache;

class SourceWindow : public QDockWidget
{
    Q_OBJECT
public:
    SourceWindow(QWidget *parent, Session* pSession);
    virtual ~SourceWindow() override;

    // Grab focus and point to the main widget
    void keyFocus();

    void loadSettings();
    void saveSettings();

private:
    void connectChanged();
    void startStopChanged();
    void startStopDelayed(int running);
    void mainStateCompleted();
    void symbolProgramChanged();
    void settingsChanged();
    void resetClicked();

    Session*            m_pSession;
    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;

    QTextEdit*          m_pSourceTextEdit;

    // Cache of files
    SourceCache*        m_pSourceCache;
    QString             m_currFileKey;
};

#endif // SOURCEWINDOW_H
