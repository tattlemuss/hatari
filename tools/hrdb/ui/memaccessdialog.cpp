#include "memaccessdialog.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "../models/targetmodel.h"
#include "../models/stringparsers.h"
#include "../models/symboltablemodel.h"
#include "../transport/dispatcher.h"
#include "quicklayout.h"
#include "colouring.h"

MemAccessDialog::MemAccessDialog(QWidget *parent, QString addressExpr, TargetModel* pTargetModel, Dispatcher* pDispatcher) :
    QDialog(),
    m_pTargetModel(pTargetModel),
    m_pDispatcher(pDispatcher)
{
    this->setWindowTitle(tr("Memory Access Breakpoint"));

    // -------------------------------
    // Change/Memory
    QLabel* pAddressLabel = new QLabel("Address:", this);
    m_pMemoryAddressEdit = new QLineEdit(this);
    m_pMemoryAddressEdit->setText(addressExpr);

    // (sizes)
    QLabel* pMemoryChangeLabel = new QLabel("Size:", this);
    m_pMemorySizeComboBox = new QComboBox(this);
    m_pMemorySizeComboBox->addItem("Byte", 0);
    m_pMemorySizeComboBox->addItem("Word", 1);
    m_pMemorySizeComboBox->addItem("Long", 2);
    m_pMemorySizeComboBox->setCurrentIndex(1);

    m_pSymbolTableModel = new SymbolTableModel(this, m_pTargetModel->GetSymbolTable());
    QCompleter* pCompl = new QCompleter(m_pSymbolTableModel, this);
    pCompl->setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);
    //m_pMemoryAddressEdit->setCompleter(pCompl);

    // -------------------------------
    // Options
    m_pReadCheckBox = new QCheckBox("Read", this);
    m_pWriteCheckBox = new QCheckBox("Write", this);
    m_pWriteCheckBox->setChecked(true);
    // -------------------------------
    m_pSetButton = new QPushButton("Set", this);
    QPushButton* pCancelButton = new QPushButton("&Cancel", this);
    m_pSetButton->setDefault(true);

    // One row for the cancel button
    QHBoxLayout* pHLayout = new QHBoxLayout(this);
    pHLayout->addWidget(m_pSetButton);
    pHLayout->addWidget(pCancelButton);

    QWidget* pButtonContainer = new QWidget(this);
    pButtonContainer->setLayout(pHLayout);

    // Stack container of the 3 breakpoint types
    // These arrays are null-terminated
    QWidget* pMemoryWidgets[] = {pAddressLabel, m_pMemoryAddressEdit, pMemoryChangeLabel, m_pMemorySizeComboBox, nullptr};
    QWidget* pOptionsWidgets[] = {m_pReadCheckBox, m_pWriteCheckBox, nullptr};
    auto* pMemoryBox = CreateHorizLayout(this, pMemoryWidgets);
    auto* pOptionsBox = CreateHorizLayout(this, pOptionsWidgets);

    // Final layout
    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(pMemoryBox);
    pLayout->addWidget(pOptionsBox);
    pLayout->addWidget(pButtonContainer);

    connect(m_pMemoryAddressEdit,   &QLineEdit::textEdited,   this, &MemAccessDialog::textChanged);
    connect(m_pSetButton,           &QPushButton::clicked,    this, &MemAccessDialog::setClicked);
    connect(pCancelButton,          &QPushButton::clicked,    this, &MemAccessDialog::reject);
    this->setLayout(pLayout);
    this->textChanged();
}

MemAccessDialog::~MemAccessDialog()
{

}

void MemAccessDialog::setClicked()
{
    uint32_t result;
    if (!StringParsers::ParseCpuExpression(
            m_pMemoryAddressEdit->text().toStdString().c_str(),
            result,
            m_pTargetModel->GetSymbolTable(),
            m_pTargetModel->GetRegs()))
    {
        return;
    }
    uint32_t size = 0;
    switch (m_pMemorySizeComboBox->currentIndex())
    {
    case 0: size = 1; break;
    case 1: size = 2; break;
    case 2: size = 4; break;
    default:
        return;
    }
    m_pDispatcher->SetAccessBreakpoint(m_pReadCheckBox->isChecked(),
                                       m_pWriteCheckBox->isChecked(),
                                       result, size);
    emit accept();
}

void MemAccessDialog::textChanged()
{
    uint32_t addr;
    bool success = CheckExpression(addr);
    Colouring::SetErrorState(m_pMemoryAddressEdit, success);
    m_pSetButton->setEnabled(success);
}

bool MemAccessDialog::CheckExpression(uint32_t& result) const
{
    return StringParsers::ParseCpuExpression(
            m_pMemoryAddressEdit->text().toStdString().c_str(),
            result,
            m_pTargetModel->GetSymbolTable(),
            m_pTargetModel->GetRegs());
}
