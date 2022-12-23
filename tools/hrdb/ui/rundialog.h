#ifndef RUNDIALOG_H
#define RUNDIALOG_H

#include <QObject>
#include <QDialog>
#include "../models/launcher.h"

class QLineEdit;
class QCheckBox;
class QComboBox;
class TargetModel;
class Dispatcher;
class Session;

class RunDialog : public QDialog
{
private:
    Q_OBJECT
public:
    RunDialog(QWidget* parent, Session* pSession);
    virtual ~RunDialog() override;

protected:
    virtual void showEvent(QShowEvent *event) override;
    virtual void closeEvent(QCloseEvent *event) override;

private:
    void okClicked();
    void exeClicked();
    void prgClicked();
    void workingDirectoryClicked();
    void watcherFilesClicked();
    void watcherTextChanged();
    void watcherActiveChanged();

    // Settings
    void LoadSettings();
    void SaveSettings();

    // This updates the local copy of the settings from the UI boxes
    void UpdateInternalSettingsFromUI();

    // UI elements
    QLineEdit*      m_pExecutableTextEdit;
    QLineEdit*      m_pPrgTextEdit;
    QLineEdit*      m_pArgsTextEdit;
    QLineEdit*      m_pWorkingDirectoryTextEdit;
    QLineEdit*      m_pWatcherFilesTextEdit;
    QCheckBox*      m_pWatcherCheckBox; 
    QComboBox*      m_pBreakModeCombo;

    // Current temporary settings to launch with
    LaunchSettings m_launchSettings;

    // Shared session data pointer (storage for launched process, temp file etc)
    Session*        m_pSession;
};

#endif // RUNDIALOG_H
