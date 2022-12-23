#ifndef SEARCHDIALOG_H
#define SEARCHDIALOG_H
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class TargetModel;

// The persistent data used by the search
class SearchSettings
{
public:
    bool CalcValues();

    enum Mode
    {
        kHex,
        kText
    };

    // Search "string" we pass to the target
    QVector<uint8_t>    m_masksAndValues;

    // These are so the UI can display what it's searching for,
    // and repeat searches
    uint32_t            m_startAddress = -1;
    uint32_t            m_endAddress = 0;       // 0 == "not set"

    Mode                m_mode = kText;
    bool                m_matchCase = false;
    QString             m_originalText;
};

class SearchDialog : public QDialog
{
private:
    Q_OBJECT
public:
    SearchDialog(QWidget *parent, const TargetModel* pTargetModel, SearchSettings& returnedSettings);
protected:
    void showEvent(QShowEvent *event);

private slots:
    void modeChangedSlot(int index);

private:
    void okClicked();
    void textEditChanged();
    void matchCaseChanged();

    bool                CheckInputs();

    const TargetModel*  m_pTargetModel;
    QLineEdit*          m_pLineEditString;
    QCheckBox*          m_pMatchCaseCheckbox;

    QLineEdit*          m_pLineEditStart;
    QLineEdit*          m_pLineEditEnd;
    QPushButton*        m_pOkButton;
    QComboBox*          m_pModeCombo;

    SearchSettings      m_localSettings;

    // Where we copy successful settings
    SearchSettings&     m_returnedSettings;
};

#endif // SEARCHDIALOG_H
