#ifndef PROFILEWINDOW_H
#define PROFILEWINDOW_H

#include <QDockWidget>
#include <QTreeView>
#include "showaddressactions.h"

class TargetModel;
class Dispatcher;
class Session;
class QLabel;
class QPushButton;
class QTextEdit;
class QComboBox;

//-----------------------------------------------------------------------------
class ProfileTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:

    struct Entry
    {
        QModelIndex     index;
        uint32_t        address;
        QString         text;
        uint32_t        instructionCount;
        uint64_t        cycleCount;
        float           cyclePercent;
    };

    enum Column
    {
        kColAddress,
        kColCycles,
        kColCyclePercent,
        kColInstructionCount,
        kColCount
    };

    enum Grouping
    {
        kGroupingSymbol,
        kGroupingAddress64,
        kGroupingAddress256,
        kGroupingAddress1024,
        kGroupingAddress4096,
    };

    ProfileTableModel(QObject * parent, TargetModel* pTargetModel, Dispatcher* pDispatcher);

    void recalc();

    // "When subclassing QAbstractTableModel, you must implement rowCount(), columnCount(), and data()."
    virtual int rowCount(const QModelIndex &parent) const override;
    virtual int columnCount(const QModelIndex &parent) const override;
    virtual QVariant data(const QModelIndex &index, int role) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    virtual void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    const Entry& GetEntry(int row) const
    {
        return entries[row];
    }

    void SetGrouping(Grouping g)
    {
        m_grouping = g;
        rebuildEntries();
    }

    Grouping GetGrouping() const
    {
        return m_grouping;
    }

private:

    void rebuildEntries();
    void populateFromEntries();

    TargetModel*    m_pTargetModel;
    Dispatcher*     m_pDispatcher;

    QMap<uint32_t, Entry>   map;
    QVector<Entry>          entries;
    int                     m_sortColumn;
    Qt::SortOrder           m_sortOrder;
    Grouping                m_grouping;
};

//-----------------------------------------------------------------------------
class ProfileTreeView : public QTreeView
{
    Q_OBJECT
public:
    ProfileTreeView(QWidget* parent, ProfileTableModel* pModel, Session* pSession);
    virtual ~ProfileTreeView() override;

public slots:

protected:
    virtual void contextMenuEvent(QContextMenuEvent *event) override;
    virtual void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    ProfileTableModel*      m_pTableModel;
    Session*                m_pSession;

    // Remembers which row we right-clicked on
    int                     m_rightClickRow;
    ShowAddressActions      m_showAddressActions;
};

class ProfileWindow : public QDockWidget
{
    Q_OBJECT
public:
    ProfileWindow(QWidget *parent, Session* pSession);
    virtual ~ProfileWindow() override;

    // Grab focus and point to the main widget
    void keyFocus();

    void loadSettings();
    void saveSettings();

private slots:
    void groupingChangedSlot(int index);

private:
    void connectChanged();
    void startStopChanged();
    void startStopDelayed(int running);
    void profileChanged();
    void settingsChanged();
    void startStopClicked();
    void resetClicked();

    Session*            m_pSession;
    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;

    QPushButton*        m_pStartStopButton;
    QPushButton*        m_pClearButton;
    QComboBox*          m_pGroupingComboBox;

    ProfileTreeView*    m_pTreeView;
    ProfileTableModel*  m_pTableModel;
};

#endif // PROFILEWINDOW_H
