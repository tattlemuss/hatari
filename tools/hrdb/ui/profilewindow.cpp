#include "profilewindow.h"

#include <iostream>
#include <QComboBox>
#include <QDebug>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../models/session.h"
#include "../models/profiledata.h"
#include "quicklayout.h"

//-----------------------------------------------------------------------------
//      Sorting comparators
//-----------------------------------------------------------------------------
bool CompCyclesAsc(ProfileTableModel::Entry& m1, ProfileTableModel::Entry& m2)
{
    return m1.cycleCount < m2.cycleCount;
}
bool CompCyclesDesc(ProfileTableModel::Entry& m1, ProfileTableModel::Entry& m2)
{
    return m1.cycleCount > m2.cycleCount;
}

bool CompCyclePercentAsc(ProfileTableModel::Entry& m1, ProfileTableModel::Entry& m2)
{
    return m1.cyclePercent < m2.cyclePercent;
}
bool CompCyclePercentDesc(ProfileTableModel::Entry& m1, ProfileTableModel::Entry& m2)
{
    return m1.cyclePercent > m2.cyclePercent;
}

bool CompCountAsc(ProfileTableModel::Entry& m1, ProfileTableModel::Entry& m2)
{
    return m1.instructionCount < m2.instructionCount;
}
bool CompCountDesc(ProfileTableModel::Entry& m1, ProfileTableModel::Entry& m2)
{
    return m1.instructionCount > m2.instructionCount;
}

bool CompAddressAsc(ProfileTableModel::Entry& m1, ProfileTableModel::Entry& m2)
{
    return m1.text < m2.text;
}
bool CompAddressDesc(ProfileTableModel::Entry& m1, ProfileTableModel::Entry& m2)
{
    return m1.text > m2.text;
}

//-----------------------------------------------------------------------------
ProfileTableModel::ProfileTableModel(QObject *parent, TargetModel *pTargetModel, Dispatcher* pDispatcher) :
    QAbstractTableModel(parent),
    m_pTargetModel(pTargetModel),
    m_pDispatcher(pDispatcher),
    m_sortColumn(kColCycles),
    m_sortOrder(Qt::DescendingOrder),
    m_grouping(kGroupingSymbol)
{
}

void ProfileTableModel::recalc()
{
    rebuildEntries();
}

//-----------------------------------------------------------------------------
int ProfileTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return entries.size();
}

//-----------------------------------------------------------------------------
int ProfileTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return kColCount;
}

//-----------------------------------------------------------------------------
QVariant ProfileTableModel::data(const QModelIndex &index, int role) const
{
    int32_t row = index.row();
    const Entry& ent = entries[row];

    if (role == Qt::DisplayRole)
    {
        if (index.column() == kColAddress)
            return ent.text;
        else if (index.column() == kColInstructionCount)
            return ent.instructionCount;
        else if (index.column() == kColCycles)
            return QVariant(static_cast<qlonglong>(ent.cycleCount));
        else if (index.column() == kColCyclePercent)
            return QVariant(ent.cyclePercent);
    }
    if (role == Qt::TextAlignmentRole)
    {
        if (index.column() == kColAddress)
            return Qt::AlignLeft;
        return Qt::AlignRight;

    }
    return QVariant(); // invalid item
}

//-----------------------------------------------------------------------------
QVariant ProfileTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Orientation::Horizontal)
    {
        if (role == Qt::DisplayRole)
        {
            switch (section)
            {
            case kColAddress:           return QString(tr("Name"));
            case kColInstructionCount:  return QString(tr("Instructions"));
            case kColCycles:            return QString(tr("Cycles"));
            case kColCyclePercent:      return QString(tr("Cycle %"));
            }
        }
        if (role == Qt::TextAlignmentRole)
        {
            if (section == kColAddress)
                return Qt::AlignLeft;
            return Qt::AlignRight;
        }
    }
    return QVariant();
}

//-----------------------------------------------------------------------------
void ProfileTableModel::sort(int column, Qt::SortOrder order)
{
    switch (column)
    {
    case kColCycles:
        std::sort(entries.begin(), entries.end(), order == Qt::SortOrder::AscendingOrder ? CompCyclesAsc : CompCyclesDesc);
        break;
    case kColInstructionCount:
        std::sort(entries.begin(), entries.end(), order == Qt::SortOrder::AscendingOrder ? CompCyclesAsc : CompCyclesDesc);
        break;
    case kColAddress:
        std::sort(entries.begin(), entries.end(), order == Qt::SortOrder::AscendingOrder ? CompAddressAsc : CompAddressDesc);
        break;
    case kColCyclePercent:
        std::sort(entries.begin(), entries.end(), order == Qt::SortOrder::AscendingOrder ? CompCyclePercentAsc : CompCyclePercentDesc);
        break;
    }
    m_sortColumn = column;
    m_sortOrder = order;
    populateFromEntries();
}

//-----------------------------------------------------------------------------
void ProfileTableModel::rebuildEntries()
{
    map.clear();
    QString text;
    const ProfileData& data = m_pTargetModel->GetRawProfileData();
    const SymbolTable& symbols = m_pTargetModel->GetSymbolTable();
    Symbol result;

    // Generate total cycles
    uint64_t cycleTotal = 0;
    for (const ProfileData::Pair ent : data.m_entries)
        cycleTotal += ent.second.cycles;

    // Protect against zero-divide
    if (cycleTotal == 0)
        cycleTotal = 1;

    uint32_t bits;
    switch (m_grouping)
    {
    case Grouping::kGroupingAddress64: bits = 6; break;
    case Grouping::kGroupingAddress256: bits = 8; break;
    case Grouping::kGroupingAddress1024: bits = 10; break;
    case Grouping::kGroupingAddress4096: bits = 12; break;
    default: bits = 0; break;
    }

    uint32_t mask = 0xffffffff << bits;
    uint32_t rest = 0xffffffff ^ mask;

    // This converts the entry to a label or a memory block
    for (const ProfileData::Pair ent : data.m_entries)
    {
        uint32_t addr = 0;
        QString label;

        if (m_grouping == kGroupingSymbol)
        {
            if (!symbols.FindLowerOrEqual(ent.first, true, result))
                continue;

            addr = result.address;
            label = QString::fromStdString(result.name);
        }
        else
        {
            // Group by bytes, rounding down the bits
            addr = ent.first & mask;
        }

        QMap<uint32_t, Entry>::iterator it = map.find(addr);
        if (it == map.end())
        {
            // New entry, so create a label here
            if (m_grouping == kGroupingSymbol)
                label = QString::fromStdString(result.name);
            else
                label = QString::asprintf("$%08x-$%08x", addr, addr + rest);

            Entry entry;
            entry.address = addr;
            entry.text = label;
            entry.instructionCount = ent.second.count;
            entry.cycleCount = ent.second.cycles;
            entry.cyclePercent = 0; // calculated at end
            map.insert(addr, entry);
        }
        else {
            it->instructionCount += ent.second.count;
            it->cycleCount += ent.second.cycles;
        }
    }
    entries.clear();
    for (auto it : map)
    {
        uint64_t scaledPercent = it.cycleCount * 1000 / cycleTotal;
        it.cyclePercent = static_cast<float>(scaledPercent) / 10.f;
        entries.push_back(it);
    }

    sort(m_sortColumn, m_sortOrder);
    // Don't need to "populate", that's called in sort()
}

//-----------------------------------------------------------------------------
void ProfileTableModel::populateFromEntries()
{
    // This is rubbish and will lose cursor position etc
    emit beginResetModel();
    emit endResetModel();
}

//-----------------------------------------------------------------------------
//----------------------------------------------------------------------------
ProfileTreeView::ProfileTreeView(QWidget* parent, ProfileTableModel* pModel, Session* pSession) :
    QTreeView(parent),
    m_pTableModel(pModel),
    m_pSession(pSession),
    m_rightClickRow(-1)
{
    // This table gets the focus from the parent docking widget
    setFocus();
}

//-----------------------------------------------------------------------------
ProfileTreeView::~ProfileTreeView()
{

}

//-----------------------------------------------------------------------------
void ProfileTreeView::contextMenuEvent(QContextMenuEvent *event)
{
    // Right click menus are instantiated on demand, so we can
    // dynamically add to them
    QMenu menu(this);

    // Add the default actions
    QMenu* pAddressMenu = nullptr;
    QModelIndex ind = indexAt(event->pos());
    if (!ind.isValid())
        return;

    int rowIdx = ind.row();
    if (rowIdx >= 0)
    {
        const ProfileTableModel::Entry& ent = m_pTableModel->GetEntry(rowIdx);

        pAddressMenu = new QMenu("", &menu);
        pAddressMenu->setTitle(QString::asprintf("Address $%08x", ent.address));

        m_showAddressActions.addActionsToMenu(pAddressMenu);
        m_showAddressActions.setAddress(m_pSession, MEM_CPU, ent.address);
        menu.addMenu(pAddressMenu);

        // Run it
        menu.exec(event->globalPos());
    }
}

//-----------------------------------------------------------------------------
void ProfileTreeView::mouseDoubleClickEvent(QMouseEvent *event)
{
    QModelIndex ind = indexAt(event->pos());
    if (!ind.isValid())
        return;

    int rowIdx = ind.row();
    if (rowIdx >= 0)
    {
        const ProfileTableModel::Entry& ent = m_pTableModel->GetEntry(rowIdx);
        emit m_pSession->addressRequested(Session::kDisasmWindow, 0, MEM_CPU, ent.address);
    }
}

//-----------------------------------------------------------------------------
ProfileWindow::ProfileWindow(QWidget *parent, Session* pSession) :
    QDockWidget(parent),
    m_pSession(pSession),
    m_pTargetModel(pSession->m_pTargetModel),
    m_pDispatcher(pSession->m_pDispatcher)
{
    this->setWindowTitle("Profile");
    setObjectName("Profile");

    // Layouts
    QVBoxLayout* pMainLayout = new QVBoxLayout;
    QHBoxLayout* pTopLayout = new QHBoxLayout;
    auto pMainRegion = new QWidget(this);   // whole panel
    auto pTopRegion = new QWidget(this);      // top buttons/edits

    m_pStartStopButton = new QPushButton("Start", this);
    m_pClearButton = new QPushButton("Clear", this);
    m_pGroupingComboBox = new QComboBox(this);

    m_pTableModel = new ProfileTableModel(this, m_pTargetModel, m_pDispatcher);
    m_pTreeView = new ProfileTreeView(this, m_pTableModel, m_pSession);
    m_pTreeView->setModel(m_pTableModel);
    m_pTreeView->setSelectionBehavior(QAbstractItemView::SelectionBehavior::SelectRows);
    m_pTreeView->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);
    m_pTreeView->setWordWrap(false);
    m_pTreeView->setSortingEnabled(true);
    m_pTreeView->sortByColumn(ProfileTableModel::kColCycles, Qt::SortOrder::DescendingOrder);
    m_pTreeView->header()->setSectionResizeMode(ProfileTableModel::kColAddress, QHeaderView::ResizeToContents);
    m_pTreeView->header()->setStretchLastSection(false);

    m_pGroupingComboBox->addItem(tr("Symbols"), ProfileTableModel::Grouping::kGroupingSymbol);
    m_pGroupingComboBox->addItem(tr("64 Bytes"), ProfileTableModel::Grouping::kGroupingAddress64);
    m_pGroupingComboBox->addItem(tr("256 Bytes"), ProfileTableModel::Grouping::kGroupingAddress256);
    m_pGroupingComboBox->addItem(tr("1024 Bytes"), ProfileTableModel::Grouping::kGroupingAddress1024);
    m_pGroupingComboBox->addItem(tr("4096 Bytes"), ProfileTableModel::Grouping::kGroupingAddress4096);

    pTopLayout->addWidget(m_pStartStopButton);
    pTopLayout->addWidget(m_pClearButton);
    pTopLayout->addWidget(new QLabel(tr("Grouping:"), this));
    pTopLayout->addWidget(m_pGroupingComboBox);
    pTopLayout->addStretch();

    pMainLayout->addWidget(pTopRegion);
    pMainLayout->addWidget(m_pTreeView);

    SetMargins(pTopLayout);
    SetMargins(pMainLayout);

    pTopRegion->setLayout(pTopLayout);
    pMainRegion->setLayout(pMainLayout);
    setWidget(pMainRegion);

    loadSettings();

    connect(m_pTargetModel,     &TargetModel::connectChangedSignal,     this, &ProfileWindow::connectChanged);
    connect(m_pTargetModel,     &TargetModel::startStopChangedSignal,   this, &ProfileWindow::startStopChanged);
    connect(m_pTargetModel,     &TargetModel::startStopChangedSignalDelayed,   this, &ProfileWindow::startStopDelayed);
    connect(m_pTargetModel,     &TargetModel::profileChangedSignal,     this, &ProfileWindow::profileChanged);
    connect(m_pSession,         &Session::settingsChanged,              this, &ProfileWindow::settingsChanged);

    connect(m_pStartStopButton, &QAbstractButton::clicked,              this, &ProfileWindow::startStopClicked);
    connect(m_pClearButton,     &QAbstractButton::clicked,              this, &ProfileWindow::resetClicked);
    connect(m_pGroupingComboBox,SIGNAL(currentIndexChanged(int)),       SLOT(groupingChangedSlot(int)));

    // Refresh enable state
    connectChanged();

    // Refresh font
    settingsChanged();
}

ProfileWindow::~ProfileWindow()
{
}

void ProfileWindow::keyFocus()
{
    activateWindow();
}

void ProfileWindow::loadSettings()
{
    QSettings settings;
    settings.beginGroup("Profile");

    restoreGeometry(settings.value("geometry").toByteArray());

    ProfileTableModel::Grouping g = static_cast<ProfileTableModel::Grouping>(settings.value("grouping", QVariant((int)ProfileTableModel::Grouping::kGroupingAddress256)).toInt());
    m_pGroupingComboBox->setCurrentIndex(g);
    m_pTableModel->SetGrouping(g);
    settings.endGroup();
}

void ProfileWindow::saveSettings()
{
    QSettings settings;
    settings.beginGroup("Profile");

    settings.setValue("geometry", saveGeometry());
    settings.setValue("grouping", static_cast<int>(m_pTableModel->GetGrouping()));
    settings.endGroup();
}

void ProfileWindow::connectChanged()
{
    bool enable = m_pTargetModel->IsConnected() && !m_pTargetModel->IsRunning();
    m_pStartStopButton->setEnabled(enable);

    m_pClearButton->setEnabled(m_pTargetModel->IsConnected());
    if (!m_pTargetModel->IsConnected())
        m_pTargetModel->ProfileReset();
}

void ProfileWindow::startStopChanged()
{
    bool enable = m_pTargetModel->IsConnected() && !m_pTargetModel->IsRunning();
    m_pStartStopButton->setEnabled(enable);
}

void ProfileWindow::startStopDelayed(int running)
{
    if (m_pTargetModel->IsConnected() && !running)
    {
        m_pTableModel->recalc();
        m_pTreeView->resizeColumnToContents(0);
    }
}

void ProfileWindow::profileChanged()
{
    if (m_pTargetModel->IsProfileEnabled())
    {
        m_pStartStopButton->setText("Stop");
    }
    else {
        m_pStartStopButton->setText("Start");
    }
}

void ProfileWindow::settingsChanged()
{
    QFontMetrics fm(m_pSession->GetSettings().m_font);

    // Down the side
    m_pTreeView->setFont(m_pSession->GetSettings().m_font);
}

void ProfileWindow::startStopClicked()
{
    if (!m_pTargetModel->IsConnected())
        return;

    m_pDispatcher->SetProfileEnable(!m_pTargetModel->IsProfileEnabled());
}

void ProfileWindow::resetClicked()
{
    m_pTargetModel->ProfileReset();
    m_pTableModel->recalc();
}

void ProfileWindow::groupingChangedSlot(int index)
{
    int modeInt = m_pGroupingComboBox->itemData(index).toInt();
    m_pTableModel->SetGrouping(static_cast<ProfileTableModel::Grouping>(modeInt));
}
