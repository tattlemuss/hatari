#ifndef ADDBREAKPOINTDIALOG_H
#define ADDBREAKPOINTDIALOG_H

#include <QDialog>

class QCheckBox;
class QLineEdit;
class TargetModel;
class Dispatcher;

class AddBreakpointDialog : public QDialog
{
   // Q_OBJECT
public:
    AddBreakpointDialog(QWidget* parent, TargetModel* pTargetModel, Dispatcher* pDispatcher);
    virtual ~AddBreakpointDialog();

protected:
    void showEvent(QShowEvent *event);

private slots:
    void okClicked();

private:
    TargetModel* m_pTargetModel;
    Dispatcher* m_pDispatcher;

    QLineEdit*  m_pExpressionEdit;
    QCheckBox*  m_pOnceCheckBox;
};

#endif // ADDBREAKPOINTDIALOG_H
