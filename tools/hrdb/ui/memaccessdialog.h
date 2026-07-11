#ifndef MEMACCESS_DIALOG_H
#define MEMACCESS_DIALOG_H

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;

class TargetModel;
class Dispatcher;
class SymbolTableModel;

class MemAccessDialog : public QDialog
{
    Q_OBJECT
public:
    MemAccessDialog(QWidget* parent, QString addressExpr, TargetModel* pTargetModel, Dispatcher* pDispatcher);
    virtual ~MemAccessDialog();

protected:

private:
    void setClicked();
    void textChanged();
    bool CheckExpression(uint32_t& result) const;

    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;

    // 1 = memory
    QLineEdit*          m_pMemoryAddressEdit;
    QComboBox*          m_pMemorySizeComboBox;

    QCheckBox*          m_pReadCheckBox;
    QCheckBox*          m_pWriteCheckBox;
    QPushButton*        m_pSetButton;

    SymbolTableModel*   m_pSymbolTableModel;
};

#endif // MEMACCESS_DIALOG_H
