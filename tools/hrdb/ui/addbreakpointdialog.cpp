#include "addbreakpointdialog.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "../models/targetmodel.h"
#include "../models/stringparsers.h"
#include "../models/symboltablemodel.h"
#include "../transport/dispatcher.h"
#include "quicklayout.h"

// Describes the "event" breakpoints
struct EventBpDesc
{
    const char* name;
    const char* bpExpression;
};

static const EventBpDesc g_eventBpDescs[] = {
    { "VBL", "pc = ($70)" },
    { "HBL", "pc = ($68)" },
    { "MFP: Centronics", "pc = ($100)" },
    { "MFP: DCD", "pc = ($104)" },
    { "MFP: CTS", "pc = ($108)" },
    { "MFP: Blitter", "pc = ($10c)" },
    { "MFP: Timer D", "pc = ($110)" },
    { "MFP: Timer C", "pc = ($114)" },
    { "MFP: ACIA (IKBD/MIDI)", "pc = ($118)" },
    { "MFP: Floppy Disk", "pc = ($11c)" },
    { "MFP: Timer B", "pc = ($120)" },
    { "MFP: Send Error", "pc = ($124)" },
    { "MFP: Send Emp", "pc = ($128)" },
    { "MFP: Receive Error", "pc = ($12c)" },
    { "MFP: Receive Full", "pc = ($130)" },
    { "MFP: Timer A", "pc = ($134)" },
    { "MFP: RINGD", "pc = ($138)" },
    { "MFP: Mono", "pc = ($13c)" },
    { "Bootsector start", "pc = ($4c6)" },
    {nullptr, nullptr}
};

AddBreakpointDialog::AddBreakpointDialog(QWidget *parent, TargetModel* pTargetModel, Dispatcher* pDispatcher) :
    QDialog(parent),
    m_pTargetModel(pTargetModel),
    m_pDispatcher(pDispatcher)
{
    this->setWindowTitle(tr("Add Breakpoint"));

    // -------------------------------
    // Main expression
    QLabel* pExpLabel = new QLabel("Expression:", this);
    m_pExpressionEdit = new QLineEdit(this);
    QPushButton* pExpressionSetButton = new QPushButton("Set", this);

    // -------------------------------
    // Change/Memory
    QLabel* pAddressLabel = new QLabel("Address:", this);
    m_pMemoryAddressEdit = new QLineEdit(this);
    QRadioButton* pButtonB = new QRadioButton(".B", this);
    QRadioButton* pButtonW = new QRadioButton(".W", this);
    QRadioButton* pButtonL = new QRadioButton(".L", this);
    pButtonW->setChecked(true);
    m_pMemorySizeButtonGroup = new QButtonGroup(this);
    m_pMemorySizeButtonGroup->addButton(pButtonB, 0);
    m_pMemorySizeButtonGroup->addButton(pButtonW, 1);
    m_pMemorySizeButtonGroup->addButton(pButtonL, 2);
    QLabel* pMemoryChangeLabel = new QLabel("changes", this);
    QPushButton* pMemorySetButton = new QPushButton("Set", this);

    // Event
    QLabel* pEventLabel = new QLabel("Event:", this);
    m_pEventCombo = new QComboBox(this);
    const EventBpDesc* pEvents = g_eventBpDescs;
    while (pEvents->name)
    {
        m_pEventCombo->addItem(QString(pEvents->name));
        ++pEvents;
    }

    QPushButton* pEventSetButton = new QPushButton("Set", this);

    QWidget* pSizeWidgets[] = {pButtonB, pButtonW, pButtonL, nullptr};
    QGroupBox* pMemorySizeGroupBox = CreateVertLayout(this, pSizeWidgets);
    pMemorySizeGroupBox->setFlat(false);

    m_pSymbolTableModel = new SymbolTableModel(this, m_pTargetModel->GetSymbolTable());
    QCompleter* pCompl = new QCompleter(m_pSymbolTableModel, this);
    pCompl->setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);
    m_pMemoryAddressEdit->setCompleter(pCompl);

    // -------------------------------
    // Options
    m_pOnceCheckBox = new QCheckBox("Once", this);
    m_pTraceCheckBox = new QCheckBox("Trace Only", this);

    // -------------------------------
    QPushButton* pCancelButton = new QPushButton("&Cancel", this);
    pCancelButton->setDefault(true);

    // These arrays are null-terminated
    QWidget* pRow1[] = {pExpLabel, m_pExpressionEdit, pExpressionSetButton, nullptr};
    QWidget* pRow2[] = {pAddressLabel, m_pMemoryAddressEdit, pMemorySizeGroupBox, pMemoryChangeLabel, pMemorySetButton, nullptr};
    QWidget* pRow2b[] = {pEventLabel, m_pEventCombo, pEventSetButton, nullptr};
    QWidget* pRow3[] = {m_pOnceCheckBox, m_pTraceCheckBox, nullptr};

    QLabel* pArgumentLink = new QLabel(this);
    pArgumentLink->setText("<a href=\"https://hatari.tuxfamily.org/doc/debugger.html#Breakpoint_conditions\">Expression Syntax Help</a>");
    pArgumentLink->setOpenExternalLinks(true);
    pArgumentLink->setTextInteractionFlags(Qt::LinksAccessibleByKeyboard|Qt::LinksAccessibleByMouse);
    pArgumentLink->setTextFormat(Qt::RichText);

    // One row for the cancel button
    QHBoxLayout* pHLayout = new QHBoxLayout(this);
    pHLayout->addWidget(pCancelButton);

    QWidget* pButtonContainer = new QWidget(this);
    pButtonContainer->setLayout(pHLayout);

    QTabWidget* pTabWidget = new QTabWidget(this);
    pTabWidget->addTab(CreateHorizLayout(this, pRow1), "Expression");
    pTabWidget->addTab(CreateHorizLayout(this, pRow2), "Memory");
    pTabWidget->addTab(CreateHorizLayout(this, pRow2b), "Interrupts");

    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(pTabWidget);
    pLayout->addWidget(CreateHorizLayout(this, pRow3));
    pLayout->addWidget(pArgumentLink);
    pLayout->addWidget(pButtonContainer);

    connect(pMemorySetButton,       &QPushButton::clicked, this, &AddBreakpointDialog::memorySetClicked);
    connect(pEventSetButton,        &QPushButton::clicked, this, &AddBreakpointDialog::eventSetClicked);
    connect(pExpressionSetButton,   &QPushButton::clicked, this, &AddBreakpointDialog::expressionOkClicked);
    connect(pCancelButton,          &QPushButton::clicked, this, &AddBreakpointDialog::reject);
    this->setLayout(pLayout);
}

AddBreakpointDialog::~AddBreakpointDialog()
{

}

void AddBreakpointDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
}

void AddBreakpointDialog::expressionOkClicked()
{
    if (m_pTargetModel->IsConnected())
    {
        // Create an expression string
        m_pDispatcher->SetBreakpoint(kProcCpu, m_pExpressionEdit->text().toStdString(), GetFlags());
        emit accept();
    }
}

void AddBreakpointDialog::memorySetClicked()
{
    const char* sizeStrings[3] =
    {
        "b", "w", "l"
    };

    int sizeId = m_pMemorySizeButtonGroup->checkedId();
    if (sizeId < 0)
        return;

    uint32_t result;
    if (StringParsers::ParseCpuExpression(m_pMemoryAddressEdit->text().toStdString().c_str(),
                                       result,
                                       m_pTargetModel->GetSymbolTable(),
                                       m_pTargetModel->GetRegs()))
    {
        QString addr = QString::asprintf("($%x).%s", result, sizeStrings[sizeId]);
        QString expr = addr + " ! " + addr;
        m_pDispatcher->SetBreakpoint(kProcCpu, expr.toStdString(), GetFlags());
        emit accept();
    }
}

void AddBreakpointDialog::eventSetClicked()
{
    int choice = m_pEventCombo->currentIndex();
    // Just copy the bp string from the description
    // TODO: if we want to check VBR we can fiddle this manually
    QString expr = QString(g_eventBpDescs[choice].bpExpression);
    m_pDispatcher->SetBreakpoint(kProcCpu, expr.toStdString(), GetFlags());
    emit accept();
}

uint64_t AddBreakpointDialog::GetFlags() const
{
    uint64_t flags = Dispatcher::kBpFlagNone;
    if (m_pOnceCheckBox->isChecked())
        flags |= Dispatcher::kBpFlagOnce;
    if (m_pTraceCheckBox->isChecked())
        flags |= Dispatcher::kBpFlagTrace;
    return flags;
}
