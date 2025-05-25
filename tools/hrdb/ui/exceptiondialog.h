#ifndef EXCEPTIONDIALOG_H
#define EXCEPTIONDIALOG_H

#include <QDialog>
#include <QGroupBox>
#include "../models/exceptionmask.h"

class QCheckBox;
class TargetModel;
class Dispatcher;

class ExceptionsGroupBox : public QGroupBox
{
    Q_OBJECT
public:
    ExceptionsGroupBox(QString title, QWidget* parent);
    void Set(ExceptionMask::Type type, bool enabled);
    bool Get(ExceptionMask::Type type) const;

private:
    void selectAllClicked();
    void selectNoneClicked();

    QPushButton* m_pSelectAllButton;
    QPushButton* m_pSelectNoneButton;
    QCheckBox* m_pCheckboxes[ExceptionMask::kExceptionCount];
};

class ExceptionDialog : public QDialog
{
    Q_OBJECT
public:
    ExceptionDialog(QWidget* parent, TargetModel* pTargetModel, Dispatcher* pDispatcher);
    virtual ~ExceptionDialog();

protected:
    void showEvent(QShowEvent *event);

private:
    void okClicked();

    TargetModel* m_pTargetModel;
    Dispatcher* m_pDispatcher;

    ExceptionsGroupBox* m_pGroupBox;
};

#endif // EXCEPTIONDIALOG_H
