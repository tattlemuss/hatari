#ifndef EXCEPTIONDIALOG_H
#define EXCEPTIONDIALOG_H

#include <QDialog>
#include "../models/exceptionmask.h"

class QCheckBox;
class TargetModel;
class Dispatcher;

class ExceptionDialog : public QDialog
{
   // Q_OBJECT
public:
    ExceptionDialog(QWidget* parent, TargetModel* pTargetModel, Dispatcher* pDispatcher);
    virtual ~ExceptionDialog();

protected:
    void showEvent(QShowEvent *event);

private slots:
    void okClicked();

private:
    QCheckBox* m_pCheckboxes[ExceptionMask::kExceptionCount];

    TargetModel* m_pTargetModel;
    Dispatcher* m_pDispatcher;
};

#endif // EXCEPTIONDIALOG_H
