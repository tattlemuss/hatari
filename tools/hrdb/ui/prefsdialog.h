#ifndef PrefsDialog_H
#define PrefsDialog_H

#include <QObject>
#include <QDialog>
#include "../models/session.h"

class QCheckBox;
class QComboBox;
class QLabel;
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
    void profileDisplayChanged(int index);

private:
    void okClicked();
    void squarePixelsClicked();
    void disassHexNumbersClicked();
    void liveRefreshClicked();
    void fontSelectClicked();

    // Make the UI reflect the stored settings (copy)
    void UpdateUIElements();

    // UI elements
    QCheckBox*      m_pGraphicsSquarePixels;
    QCheckBox*      m_pDisassHexNumerics;
    QComboBox*      m_pProfileDisplayCombo;
    QCheckBox*      m_pLiveRefresh;
    QLabel*         m_pFontLabel;

    // Shared session data pointer (storage for launched process, temp file etc)
    Session*        m_pSession;

    Session::Settings   m_settingsCopy;
};

#endif // PrefsDialog_H
