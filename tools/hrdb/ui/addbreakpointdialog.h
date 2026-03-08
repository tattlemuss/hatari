#ifndef ADDBREAKPOINTDIALOG_H
#define ADDBREAKPOINTDIALOG_H

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QComboBox;
class QButtonGroup;
class QStackedWidget;

class TargetModel;
class Dispatcher;
class SymbolTableModel;

class AddBreakpointDialog : public QDialog
{
    Q_OBJECT
public:
    AddBreakpointDialog(QWidget* parent, TargetModel* pTargetModel, Dispatcher* pDispatcher);
    virtual ~AddBreakpointDialog();

protected:
    void showEvent(QShowEvent *event);

private:
    void typeActivated();
    void setClicked();

    uint64_t GetFlags() const;

    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;

    QButtonGroup*       m_pBpTypeButtonGroup;
    QStackedWidget*     m_pStackedWidget;

    // 0 = expression
    QLineEdit*          m_pExpressionEdit;

    // 1 = memory
    QLineEdit*          m_pMemoryAddressEdit;
    QButtonGroup*       m_pMemorySizeButtonGroup;

    // 2 - interrupt
    QComboBox*          m_pInterruptCombo;

    QCheckBox*          m_pOnceCheckBox;
    QCheckBox*          m_pTraceCheckBox;
    SymbolTableModel*   m_pSymbolTableModel;
};

#endif // ADDBREAKPOINTDIALOG_H
