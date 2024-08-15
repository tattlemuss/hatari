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

    QLabel* pChangeLabel = new QLabel("changes", this);

    QWidget* pSizeWidgets[] = {pButtonB, pButtonW, pButtonL, nullptr};
    QGroupBox* pMemorySizeGroupBox = CreateVertLayout(this, pSizeWidgets);
    pMemorySizeGroupBox->setFlat(false);
    QPushButton* pChangeUseButton = new QPushButton("Use", this);

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

    QWidget* pRow1[] = {pExpLabel, m_pExpressionEdit, nullptr};
    QWidget* pRow2[] = {pAddressLabel, m_pMemoryAddressEdit, pMemorySizeGroupBox, pChangeLabel, pChangeUseButton, nullptr};
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
    pLayout->addWidget(CreateHorizLayout(this, pRow3));
    pLayout->addWidget(pArgumentLink);
    pLayout->addWidget(pButtonContainer);

    connect(pChangeUseButton, &QPushButton::clicked, this, &AddBreakpointDialog::useClicked);

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

void AddBreakpointDialog::useClicked()
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
