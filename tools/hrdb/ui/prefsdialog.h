#ifndef PrefsDialog_H
#define PrefsDialog_H

#include <QObject>
#include <QDialog>
#include "../models/session.h"

class QCheckBox;
class TargetModel;
class Dispatcher;
class Session;

class PrefsDialog : public QDialog
{
private:
    Q_OBJECT
public:
    PrefsDialog(QWidget* parent, Session* pSession);
    virtual ~PrefsDialog() override;

    // Settings
    void loadSettings();
    void saveSettings();

protected:
    virtual void showEvent(QShowEvent *event) override;
    virtual void closeEvent(QCloseEvent *event) override;

private slots:
    void okClicked();
    void squarePixelsClicked();
    void fontSelectClicked();

private:
    // Make the UI reflect the stored settings (copy)
    void UpdateUIElements();

    // UI elements
    QCheckBox*      m_pGraphicsSquarePixels;

    // Shared session data pointer (storage for launched process, temp file etc)
    Session*        m_pSession;

    Session::Settings   m_settingsCopy;
};

#endif // PrefsDialog_H
