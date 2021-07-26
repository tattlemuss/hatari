#ifndef RUNDIALOG_H
#define RUNDIALOG_H

#include <QDialog>

class QLineEdit;
class TargetModel;
class Dispatcher;

class RunDialog : public QDialog
{
   // Q_OBJECT
public:
    RunDialog(QWidget* parent, TargetModel* pTargetModel, Dispatcher* pDispatcher);
    virtual ~RunDialog();

protected:
    void showEvent(QShowEvent *event);

private slots:
    void okClicked();

private:

    QLineEdit*      m_pExecutableTextEdit;
    QLineEdit*      m_pArgsTextEdit;

    TargetModel* m_pTargetModel;
    Dispatcher* m_pDispatcher;
};

#endif // RUNDIALOG_H
