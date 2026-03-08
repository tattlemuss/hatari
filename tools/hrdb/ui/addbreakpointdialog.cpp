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
#include <QStackedWidget>

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

    // (sizes)
    QRadioButton* pButtonB = new QRadioButton(".B", this);
    QRadioButton* pButtonW = new QRadioButton(".W", this);
    QRadioButton* pButtonL = new QRadioButton(".L", this);
    pButtonW->setChecked(true);
    m_pMemorySizeButtonGroup = new QButtonGroup(this);
    m_pMemorySizeButtonGroup->addButton(pButtonB, 0);
    m_pMemorySizeButtonGroup->addButton(pButtonW, 1);
    m_pMemorySizeButtonGroup->addButton(pButtonL, 2);
    QWidget* pSizeWidgets[] = {pButtonB, pButtonW, pButtonL, nullptr};
    QGroupBox* pMemorySizeGroupBox = CreateVertLayout(this, pSizeWidgets);
    pMemorySizeGroupBox->setFlat(false);

    QLabel* pMemoryChangeLabel = new QLabel("changes", this);

    // Event
    QLabel* pEventLabel = new QLabel("Event:", this);
    m_pInterruptCombo = new QComboBox(this);
    const EventBpDesc* pEvents = g_eventBpDescs;
    while (pEvents->name)
    {
        m_pInterruptCombo->addItem(QString(pEvents->name));
        ++pEvents;
    }

    m_pSymbolTableModel = new SymbolTableModel(this, m_pTargetModel->GetSymbolTable());
    QCompleter* pCompl = new QCompleter(m_pSymbolTableModel, this);
    pCompl->setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);
    m_pMemoryAddressEdit->setCompleter(pCompl);

    // -------------------------------
    // Options
    m_pOnceCheckBox = new QCheckBox("Once", this);
    m_pTraceCheckBox = new QCheckBox("Trace Only", this);

    // -------------------------------
    QPushButton* pSetButton = new QPushButton("Set", this);
    QPushButton* pCancelButton = new QPushButton("&Cancel", this);
    pSetButton->setDefault(true);

    QLabel* pArgumentLink = new QLabel(this);
    pArgumentLink->setText("<a href=\"https://hatari.tuxfamily.org/doc/debugger.html#Breakpoint_conditions\">Expression Syntax Help</a>");
    pArgumentLink->setOpenExternalLinks(true);
    pArgumentLink->setTextInteractionFlags(Qt::LinksAccessibleByKeyboard|Qt::LinksAccessibleByMouse);
    pArgumentLink->setTextFormat(Qt::RichText);

    // One row for the cancel button
    QHBoxLayout* pHLayout = new QHBoxLayout(this);
    pHLayout->addWidget(pSetButton);
    pHLayout->addWidget(pCancelButton);

    QWidget* pButtonContainer = new QWidget(this);
    pButtonContainer->setLayout(pHLayout);

    // Stack container of the 3 breakpoint types
    // These arrays are null-terminated
    QWidget* pExpressionWidgets[] = {pExpLabel, m_pExpressionEdit, nullptr};
    QWidget* pMemoryWidgets[] = {pAddressLabel, m_pMemoryAddressEdit, pMemorySizeGroupBox, pMemoryChangeLabel, nullptr};
    QWidget* pInterruptWidgets[] = {pEventLabel, m_pInterruptCombo, nullptr};
    auto* pExpressionBox = CreateHorizLayout(this, pExpressionWidgets);
    auto* pMemoryBox = CreateHorizLayout(this, pMemoryWidgets);
    auto* pInterruptBox = CreateHorizLayout(this, pInterruptWidgets);
    m_pStackedWidget = new QStackedWidget(this);
    m_pStackedWidget->addWidget(pExpressionBox);
    m_pStackedWidget->addWidget(pMemoryBox);
    m_pStackedWidget->addWidget(pInterruptBox);

    // Radio buttons for the breakpoint type
    QRadioButton* pButtonExpr = new QRadioButton("Expression", this);
    QRadioButton* pButtonMem = new QRadioButton("Memory", this);
    QRadioButton* pButtonInt = new QRadioButton("Interrupt", this);
    pButtonExpr->setChecked(true);
    m_pBpTypeButtonGroup = new QButtonGroup(this);
    m_pBpTypeButtonGroup->addButton(pButtonExpr, 0);
    m_pBpTypeButtonGroup->addButton(pButtonMem, 1);
    m_pBpTypeButtonGroup->addButton(pButtonInt, 2);
    QWidget* pTypeWidgets[] = {pButtonExpr, pButtonMem, pButtonInt, nullptr};
    QGroupBox* m_pBreakpointTypeGroupBox;
    m_pBreakpointTypeGroupBox = CreateHorizLayout(this, pTypeWidgets);
    m_pBreakpointTypeGroupBox->setTitle("Breakpoint type");

    QWidget* pRow3[] = {m_pOnceCheckBox, m_pTraceCheckBox, nullptr};

    // Final layout
    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(m_pBreakpointTypeGroupBox);
    pLayout->addWidget(m_pStackedWidget);
    pLayout->addWidget(CreateHorizLayout(this, pRow3));
    pLayout->addWidget(pArgumentLink);
    pLayout->addWidget(pButtonContainer);

    connect(m_pBpTypeButtonGroup,   &QButtonGroup::idClicked, this, &AddBreakpointDialog::typeActivated);
    connect(pSetButton,             &QPushButton::clicked,    this, &AddBreakpointDialog::setClicked);
    connect(pCancelButton,          &QPushButton::clicked,    this, &AddBreakpointDialog::reject);
    this->setLayout(pLayout);
}

AddBreakpointDialog::~AddBreakpointDialog()
{

}

void AddBreakpointDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
}

void AddBreakpointDialog::typeActivated()
{
    // Switch the stacked widget to show the correct editor
    m_pStackedWidget->setCurrentIndex(m_pBpTypeButtonGroup->checkedId());
}

void AddBreakpointDialog::setClicked()
{
    QString bpExpr;
    switch (m_pStackedWidget->currentIndex())
    {
    case 0:
        bpExpr = m_pExpressionEdit->text();
        break;
    case 1:
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
            bpExpr = addr + " ! " + addr;
        }
        break;
    }
    case 2:
    {
        int choice = m_pInterruptCombo->currentIndex();
        // Just copy the bp string from the description
        // TODO: if we want to check VBR we can fiddle this manually
        bpExpr = QString(g_eventBpDescs[choice].bpExpression);
        break;
    }
    default:
        assert(0);
    }

    if (bpExpr.size() != 0)
    {
        m_pDispatcher->SetBreakpoint(kProcCpu, bpExpr.toStdString(), GetFlags());
        emit accept();
    }
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
