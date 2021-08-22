#ifndef CONSOLEWINDOW_H
#define CONSOLEWINDOW_H

#include <QDockWidget>
#include <QTableView>
#include "../models/memory.h"

class TargetModel;
class Dispatcher;
class QComboBox;
class QCheckBox;

class ConsoleWindow : public QDockWidget
{
    Q_OBJECT
public:
    ConsoleWindow(QWidget *parent, TargetModel* pTargetModel, Dispatcher* m_pDispatcher);

    // Grab focus and point to the main widget
    void keyFocus();

    void loadSettings();
    void saveSettings();

private slots:
    void connectChangedSlot();
    void textEditChangedSlot();

private:
    QLineEdit*          m_pLineEdit;
    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;
};

#endif // CONSOLEWINDOW_H
