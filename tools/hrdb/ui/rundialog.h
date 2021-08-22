#ifndef RUNDIALOG_H
#define RUNDIALOG_H

#include <QObject>
#include <QDialog>

class QLineEdit;
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

    // Settings
    void loadSettings();
    void saveSettings();

protected:
    virtual void showEvent(QShowEvent *event) override;
    virtual void closeEvent(QCloseEvent *event) override;

private slots:
    void okClicked();
    void exeClicked();
    void prgClicked();
    void workingDirectoryClicked();

private:
    // UI elements
    QLineEdit*      m_pExecutableTextEdit;
    QLineEdit*      m_pPrgTextEdit;
    QLineEdit*      m_pArgsTextEdit;
    QLineEdit*      m_pWorkingDirectoryTextEdit;
    QComboBox*      m_pBreakModeCombo;

    // What sort of automatic breakpoint to use
    enum BreakMode
    {
        kNone,
        kBoot,
        kProgStart
    };

    // Shared session data pointer (storage for launched process, temp file etc)
    Session*        m_pSession;
};

#endif // RUNDIALOG_H
