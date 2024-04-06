#ifndef SAVEBINDIALOG_H
#define SAVEBINDIALOG_H
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class TargetModel;

// The persistent data used by the SaveBin
class SaveBinSettings
{
public:
    // These are so the UI can display what it's saving,
    // and repeat it
    uint32_t            m_startAddress = -1;
    uint32_t            m_sizeInBytes = 0;       // 0 == "not set"

    QString             m_filename;
};

class SaveBinDialog : public QDialog
{
private:
    Q_OBJECT
public:
    SaveBinDialog(QWidget *parent, const TargetModel* pTargetModel, SaveBinSettings& returnedSettings);
protected:
    void showEvent(QShowEvent *event);

private slots:

private:
    void okClicked();
    void textEditChanged();
    void filenameClicked();

    bool                CheckInputs();

    const TargetModel*  m_pTargetModel;

    QLineEdit*          m_pLineEditStart;
    QLineEdit*          m_pLineEditSize;
    QLineEdit*          m_pFilenameTextEdit;
    QPushButton*        m_pOkButton;

    SaveBinSettings      m_localSettings;

    // Where we copy successful settings
    SaveBinSettings&     m_returnedSettings;
};

#endif // SAVEBINDIALOG_H
