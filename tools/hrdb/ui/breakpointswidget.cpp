#include "breakpointswidget.h"

#include <iostream>
#include <QGroupBox>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QStringListModel>
#include <QFontDatabase>
#include <QCompleter>
#include <QPushButton>

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../models/stringparsers.h"
#include "../models/symboltablemodel.h"

#include "addbreakpointdialog.h"
#include "quicklayout.h"

BreakpointsTableModel::BreakpointsTableModel(QObject *parent, TargetModel *pTargetModel, Dispatcher* pDispatcher) :
    QAbstractTableModel(parent),
    m_pTargetModel(pTargetModel),
    m_pDispatcher(pDispatcher)
{
    connect(m_pTargetModel, &TargetModel::breakpointsChangedSignal, this, &BreakpointsTableModel::breakpointsChangedSlot);
}

int BreakpointsTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_pTargetModel->GetBreakpoints().m_breakpoints.size();
}

int BreakpointsTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return kColCount;
}

QVariant BreakpointsTableModel::data(const QModelIndex &index, int role) const
{
    uint32_t row = index.row();
    const Breakpoints& bps = m_pTargetModel->GetBreakpoints();
    if (row >= bps.m_breakpoints.size())
        return QVariant();
    const Breakpoint& bp = bps.m_breakpoints[row];

    if (role == Qt::DisplayRole)
    {
        if (index.column() == kColId)
            return QString::number(row + 1);
        else if (index.column() == kColExpression)
            return QString(bp.m_expression.c_str());
        else if (index.column() == kColConditionCount)
            return QString::number(bp.m_conditionCount);
        else if (index.column() == kColHitCount)
            return QString::number(bp.m_hitCount);
        else if (index.column() == kColOnce)
            return QString::number(bp.m_once);
        else if (index.column() == kColTrace)
            return QString::number(bp.m_trace);
        else if (index.column() == kColQuiet)
            return QString::number(bp.m_quiet);
    }
    if (role == Qt::TextAlignmentRole)
    {
        if (index.column() == kColExpression)
            return Qt::AlignLeft;
        return Qt::AlignRight;

    }
    return QVariant(); // invalid item
}

QVariant BreakpointsTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Orientation::Horizontal)
    {
        if (role == Qt::DisplayRole)
        {
            switch (section)
            {
            case kColId:             return QString(tr("ID"));
            case kColExpression:     return QString(tr("Expression"));
            case kColConditionCount: return QString(tr("Condition Count"));
            case kColHitCount:       return QString(tr("Hit Count"));
            case kColOnce:           return QString(tr("Once?"));
            case kColQuiet:          return QString(tr("Quiet"));
            case kColTrace:          return QString(tr("Trace"));
            }
        }
        if (role == Qt::TextAlignmentRole)
        {
            if (section == kColExpression)
                return Qt::AlignLeft;
            return Qt::AlignRight;
        }
    }
    return QVariant();
}

bool BreakpointsTableModel::GetBreakpoint(uint32_t row, Breakpoint &breakpoint)
{
    const Breakpoints& bps = m_pTargetModel->GetBreakpoints();
    if (row >= bps.m_breakpoints.size())
        return false;

    breakpoint = bps.m_breakpoints[row];
    return true;
}

void BreakpointsTableModel::breakpointsChangedSlot()
{
    // TODO: I still don't understand how to update it here
    //const Breakpoints& bps = m_pTargetModel->GetBreakpoints();
    //emit dataChanged(this->createIndex(0, 0), this->createIndex(bps.m_breakpoints.size() - 1, 1));
    emit beginResetModel();
    emit endResetModel();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
BreakpointsTableView::BreakpointsTableView(QWidget* parent, BreakpointsTableModel* pModel) :
    QTableView(parent),
    m_pTableModel(pModel),
    //m_rightClickMenu(this),
    m_rightClickRow(-1)
{
    // Actions for right-click menu
    //m_pRunUntilAction = new QAction(tr("Run to here"), this);
    //connect(m_pRunUntilAction, &QAction::triggered, this, &BreakpointsTableView::runToCursorRightClick);
    //m_pBreakpointAction = new QAction(tr("Toggle Breakpoint"), this);
    //m_rightClickMenu.addAction(m_pRunUntilAction);
    //m_rightClickMenu.addAction(m_pBreakpointAction);
    //new QShortcut(QKeySequence(tr("F3", "Run to cursor")),        this, SLOT(runToCursor()));
    //new QShortcut(QKeySequence(tr("F9", "Toggle breakpoint")),    this, SLOT(toggleBreakpoint()));

    //connect(m_pBreakpointAction, &QAction::triggered,                  this, &BreakpointsTableView::toggleBreakpointRightClick);
    //connect(pTargetModel,        &TargetModel::startStopChangedSignal, this, &BreakpointsTableView::RecalcRowCount);

    // This table gets the focus from the parent docking widget
    setFocus();
}

/*
void BreakpointsTableView::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex index = this->indexAt(event->pos());
    if (!index.isValid())
        return;

    m_rightClickRow = index.row();
    m_rightClickMenu.exec(event->globalPos());

}

void BreakpointsTableView::runToCursorRightClick()
{
    m_pTableModel->RunToRow(m_rightClickRow);
    m_rightClickRow = -1;
}

void BreakpointsTableView::toggleBreakpointRightClick()
{
    m_pTableModel->ToggleBreakpoint(m_rightClickRow);
    m_rightClickRow = -1;
}

void BreakpointsTableView::runToCursor()
{
    // How do we get the selected row
    QModelIndex i = this->currentIndex();
    m_pTableModel->RunToRow(i.row());
}

void BreakpointsTableView::toggleBreakpoint()
{
    // How do we get the selected row
    QModelIndex i = this->currentIndex();
    m_pTableModel->ToggleBreakpoint(i.row());
}
*/
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
BreakpointsWidget::BreakpointsWidget(QWidget *parent, TargetModel* pTargetModel, Dispatcher* pDispatcher) :
    QDockWidget(parent),
    m_pTargetModel(pTargetModel),
    m_pDispatcher(pDispatcher)
{
    this->setWindowTitle("Breakpoints");
    setObjectName("BreakpointsWidget");

    // Make the data first
    pModel = new BreakpointsTableModel(this, pTargetModel, pDispatcher);

    m_pTableView = new BreakpointsTableView(this, pModel);
    m_pTableView->setModel(pModel);

    m_pSymbolTableModel = new SymbolTableModel(this, m_pTargetModel->GetSymbolTable());
    QCompleter* pCompl = new QCompleter(m_pSymbolTableModel, this);
    pCompl->setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);

    const QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_pTableView->setFont(monoFont);
    QFontMetrics fm(monoFont);

    // Down the side
    m_pTableView->verticalHeader()->hide();
    m_pTableView->verticalHeader()->setDefaultSectionSize(fm.height());

    // Layouts
    QVBoxLayout* pMainLayout = new QVBoxLayout;
    auto pMainRegion = new QWidget(this);   // whole panel

    QPushButton* pAddButton = new QPushButton(tr("Add..."), this);
    m_pDeleteButton = new QPushButton(tr("Delete"), this);
    QWidget* topWidgets[] = {pAddButton, m_pDeleteButton, nullptr};

    pMainLayout->addWidget(CreateHorizLayout(this, topWidgets) );
    pMainLayout->addWidget(m_pTableView);

    pMainRegion->setLayout(pMainLayout);
    setWidget(pMainRegion);

    connect(pAddButton,      &QAbstractButton::clicked, this, &BreakpointsWidget::addBreakpointClicked);
    connect(m_pDeleteButton, &QAbstractButton::clicked, this, &BreakpointsWidget::deleteBreakpointClicked);
}

void BreakpointsWidget::keyFocus()
{
    activateWindow();
    m_pTableView->setFocus();
}

void BreakpointsWidget::addBreakpointClicked()
{
    AddBreakpointDialog dialog(this, m_pTargetModel, m_pDispatcher);
    dialog.exec();
}

void BreakpointsWidget::deleteBreakpointClicked()
{
    Breakpoint bp;
    if (pModel->GetBreakpoint(m_pTableView->currentIndex().row(), bp))
    {
        m_pDispatcher->DeleteBreakpoint(bp.m_id);
    }
}


