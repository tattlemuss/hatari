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

    // -------------------------------
    // Change/Memory
    QLabel* pAddressLabel = new QLabel("Address:", this);
    m_pMemoryAddressEdit = new QLineEdit(this);
    QRadioButton* pButtonB = new QRadioButton(".B", this);
    QRadioButton* pButtonW = new QRadioButton(".W", this);
    QRadioButton* pButtonL = new QRadioButton(".L", this);
    m_pMemorySizeButtonGroup = new QButtonGroup(this);
    m_pMemorySizeButtonGroup->addButton(pButtonB, 0);
    m_pMemorySizeButtonGroup->addButton(pButtonW, 1);
    m_pMemorySizeButtonGroup->addButton(pButtonL, 2);
    QLabel* pMemoryChangeLabel = new QLabel("changes", this);
    QPushButton* pMemoryUseButton = new QPushButton("Use", this);

    // Event
    QLabel* pEventLabel = new QLabel("Event:", this);
    m_pEventCombo = new QComboBox(this);
    const EventBpDesc* pEvents = g_eventBpDescs;
    while (pEvents->name)
    {
        m_pEventCombo->addItem(QString(pEvents->name));
        ++pEvents;
    }

    QPushButton* pEventUseButton = new QPushButton("Use", this);

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
    QPushButton* pOkButton = new QPushButton("&OK", this);
    pOkButton->setDefault(true);
    QPushButton* pCancelButton = new QPushButton("&Cancel", this);

    // These arrays are null-terminated
    QWidget* pRow1[] = {pExpLabel, m_pExpressionEdit, nullptr};
    QWidget* pRow2[] = {pAddressLabel, m_pMemoryAddressEdit, pMemorySizeGroupBox, pMemoryChangeLabel, pMemoryUseButton, nullptr};
    QWidget* pRow2b[] = {pEventLabel, m_pEventCombo, pEventUseButton, nullptr};
    QWidget* pRow3[] = {m_pOnceCheckBox, m_pTraceCheckBox, nullptr};

    QLabel* pArgumentLink = new QLabel(this);
    pArgumentLink->setText("<a href=\"https://hatari.tuxfamily.org/doc/debugger.html#Breakpoint_conditions\">Expression Syntax Help</a>");
    pArgumentLink->setOpenExternalLinks(true);
    pArgumentLink->setTextInteractionFlags(Qt::LinksAccessibleByKeyboard|Qt::LinksAccessibleByMouse);
    pArgumentLink->setTextFormat(Qt::RichText);

    QHBoxLayout* pHLayout = new QHBoxLayout(this);
    pHLayout->addWidget(pOkButton);
    pHLayout->addWidget(pCancelButton);
    QWidget* pButtonContainer = new QWidget(this);
    pButtonContainer->setLayout(pHLayout);

    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(CreateHorizLayout(this, pRow1));
    pLayout->addWidget(CreateHorizLayout(this, pRow2));
    pLayout->addWidget(CreateHorizLayout(this, pRow2b));
    pLayout->addWidget(CreateHorizLayout(this, pRow3));
    pLayout->addWidget(pArgumentLink);
    pLayout->addWidget(pButtonContainer);

    connect(pMemoryUseButton, &QPushButton::clicked, this, &AddBreakpointDialog::memoryUseClicked);
    connect(pEventUseButton,  &QPushButton::clicked, this, &AddBreakpointDialog::eventUseClicked);

    connect(pOkButton,        &QPushButton::clicked, this, &AddBreakpointDialog::okClicked);
    connect(pOkButton,        &QPushButton::clicked, this, &AddBreakpointDialog::accept);
    connect(pCancelButton,    &QPushButton::clicked, this, &AddBreakpointDialog::reject);
    this->setLayout(pLayout);
}

AddBreakpointDialog::~AddBreakpointDialog()
{

}

void AddBreakpointDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
}

void AddBreakpointDialog::okClicked()
{
    if (m_pTargetModel->IsConnected())
    {
        // Create an expression string
        uint64_t flags = Dispatcher::kBpFlagNone;
        if (m_pOnceCheckBox->isChecked())
            flags |= Dispatcher::kBpFlagOnce;
        if (m_pTraceCheckBox->isChecked())
            flags |= Dispatcher::kBpFlagTrace;

        m_pDispatcher->SetBreakpoint(kProcCpu, m_pExpressionEdit->text().toStdString(), flags);
    }
}

void AddBreakpointDialog::memoryUseClicked()
{
    const char* sizeStrings[3] =
    {
        "b", "w", "l"
    };

    uint32_t result;
    if (StringParsers::ParseCpuExpression(m_pMemoryAddressEdit->text().toStdString().c_str(),
                                       result,
                                       m_pTargetModel->GetSymbolTable(),
                                       m_pTargetModel->GetRegs()))
    {
        QString addr = QString::asprintf("($%x).%s", result, sizeStrings[m_pMemorySizeButtonGroup->checkedId()]);
        m_pExpressionEdit->setText(addr + " ! " + addr);
    }
}

void AddBreakpointDialog::eventUseClicked()
{
    int choice = m_pEventCombo->currentIndex();
    // Just copy the bp string from the description
    // TODO: if we want to check VBR we can fiddle this manually
    m_pExpressionEdit->setText(QString(g_eventBpDescs[choice].bpExpression));
}

