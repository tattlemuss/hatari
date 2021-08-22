#ifndef BREAKPOINTSWIDGET_H
#define BREAKPOINTSWIDGET_H

#include <QDockWidget>
#include <QTableView>

class TargetModel;
class Dispatcher;
class QComboBox;
class QCheckBox;
class QPushButton;

struct Breakpoint;

class BreakpointsTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column
    {
        kColId,
        kColExpression,
        kColConditionCount,
        kColHitCount,
        kColOnce,
        kColQuiet,
        kColTrace,
        kColCount
    };

    BreakpointsTableModel(QObject * parent, TargetModel* pTargetModel, Dispatcher* pDispatcher);

    // "When subclassing QAbstractTableModel, you must implement rowCount(), columnCount(), and data()."
    virtual int rowCount(const QModelIndex &parent) const;
    virtual int columnCount(const QModelIndex &parent) const;
    virtual QVariant data(const QModelIndex &index, int role) const;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const;

    bool GetBreakpoint(uint32_t row, Breakpoint& breakpoint);

public slots:
    void breakpointsChangedSlot();

private:

    TargetModel*    m_pTargetModel;
    Dispatcher*     m_pDispatcher;
};

class BreakpointsTableView : public QTableView
{
    Q_OBJECT
public:
    BreakpointsTableView(QWidget* parent, BreakpointsTableModel* pModel);

public slots:

protected:

private:

private slots:

private:
    BreakpointsTableModel*     m_pTableModel;

    // Remembers which row we right-clicked on
    int                   m_rightClickRow;
};

class BreakpointsWindow : public QDockWidget
{
    Q_OBJECT
public:
    BreakpointsWindow(QWidget *parent, TargetModel* pTargetModel, Dispatcher* m_pDispatcher);   
    void keyFocus();
public slots:

private slots:

    void connectChangedSlot();
    void addBreakpointClicked();
    void deleteBreakpointClicked();
private:
    BreakpointsTableView*     m_pTableView;
    QPushButton*              m_pAddButton;
    QPushButton*              m_pDeleteButton;

    BreakpointsTableModel* pModel;
    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;
    QAbstractItemModel* m_pSymbolTableModel;
};

#endif // BREAKPOINTSWIDGET_H
