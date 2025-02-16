#include "savebindialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QFileDialog>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QPushButton>
#include "../models/stringformat.h"
#include "../models/stringparsers.h"
#include "../models/symboltablemodel.h"
#include "../models/targetmodel.h"
#include "colouring.h"

SaveBinDialog::SaveBinDialog(QWidget *parent, const TargetModel* pTargetModel, SaveBinSettings& returnedSettings) :
    QDialog(parent),
    m_pTargetModel(pTargetModel),
    m_returnedSettings(returnedSettings)
{
    this->setWindowTitle(tr("Write Binary File..."));
    this->setObjectName("SaveBinDialog");

    // Take a copy of the input settings
    m_localSettings = returnedSettings;

    SymbolTableModel* pSymbolTableModel = new SymbolTableModel(this, m_pTargetModel->GetSymbolTable());
    QCompleter* pCompl = new QCompleter(pSymbolTableModel, this);
    pCompl->setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);

    m_pLineEditStart = new QLineEdit(this);
    m_pLineEditStart->setText(Format::to_hex32(m_localSettings.m_startAddress));
    m_pLineEditStart->setCompleter(pCompl);
    m_pLineEditSize = new QLineEdit(this);
    m_pLineEditSize->setText(Format::to_hex32(m_localSettings.m_sizeInBytes));
    m_pLineEditSize->setCompleter(pCompl);

    m_pFilenameTextEdit = new QLineEdit("", this);
    QPushButton* pFilenameButton = new QPushButton(tr("Browse..."), this);

    // Bottom OK/Cancel buttons
    m_pOkButton = new QPushButton("&OK", this);
    m_pOkButton->setDefault(true);
    QPushButton* pCancelButton = new QPushButton("&Cancel", this);
    QHBoxLayout* pHLayout = new QHBoxLayout(this);
    pHLayout->addStretch(20);
    pHLayout->addWidget(m_pOkButton);
    pHLayout->addWidget(pCancelButton);
    pHLayout->addStretch(20);
    QWidget* pButtonContainer = new QWidget(this);
    pButtonContainer->setLayout(pHLayout);

    // Options grid box
    QGroupBox* gridGroupBox = new QGroupBox(tr("Options"));
    QGridLayout *gridLayout = new QGridLayout;

    int row = 0;
    gridLayout->addWidget(new QLabel(tr("Start:")), row, 0);
    gridLayout->addWidget(m_pLineEditStart, row, 1);
    ++row;
    gridLayout->addWidget(new QLabel(tr("Size in bytes:")), row, 0);
    gridLayout->addWidget(m_pLineEditSize, row, 1);
    ++row;
    gridLayout->addWidget(new QLabel(tr("Filename:")), row, 0);
    gridLayout->addWidget(m_pFilenameTextEdit, row, 1);
    gridLayout->addWidget(pFilenameButton, row, 2);
    ++row;

    // Layout grid
    gridLayout->setColumnStretch(2, 20);
    gridGroupBox->setLayout(gridLayout);

    // Overall layout (options at top, buttons at bottom)
    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(gridGroupBox);
    pLayout->addWidget(pButtonContainer);
    m_pLineEditStart->setFocus();

    CheckInputs();

    connect(m_pLineEditStart,     &QLineEdit::textChanged,        this, &SaveBinDialog::textEditChanged);
    connect(m_pLineEditSize,      &QLineEdit::textChanged,        this, &SaveBinDialog::textEditChanged);
    connect(m_pFilenameTextEdit,  &QLineEdit::textEdited,         this, &SaveBinDialog::textEditChanged);
    connect(pFilenameButton,      &QPushButton::clicked,          this, &SaveBinDialog::filenameClicked);

    connect(m_pOkButton, &QPushButton::clicked, this, &SaveBinDialog::okClicked);
    connect(m_pOkButton, &QPushButton::clicked, this, &SaveBinDialog::accept);
    connect(pCancelButton, &QPushButton::clicked, this, &SaveBinDialog::reject);
}

void SaveBinDialog::showEvent(QShowEvent *event)
{
    m_pFilenameTextEdit->setText(m_localSettings.m_filename);
    QDialog::showEvent(event);
}

void SaveBinDialog::filenameClicked()
{
    QFileDialog dialog(this,
                       tr("Choose output filename"));
    QStringList fileNames;
    if (dialog.exec())
    {
        fileNames = dialog.selectedFiles();
        if (fileNames.length() > 0)
        {
            QString name = QDir::toNativeSeparators(fileNames[0]);
            m_pFilenameTextEdit->setText(name);
            CheckInputs();
            m_returnedSettings = m_localSettings;
        }
    }

}

void SaveBinDialog::okClicked()
{
    if (!CheckInputs())
        return;

    m_returnedSettings = m_localSettings;
}

void SaveBinDialog::textEditChanged()
{
    CheckInputs();
}

// Copy UI elements into local settings when possible, and highlight "bad" inputs.
bool SaveBinDialog::CheckInputs()
{
    bool valid = true;
    bool startValid = true;
    bool sizeValid = true;
    bool fileValid = false;

    uint32_t addr;
    uint32_t size;
    startValid = StringParsers::ParseCpuExpression(m_pLineEditStart->text().toStdString().c_str(), addr,
                                       m_pTargetModel->GetSymbolTable(), m_pTargetModel->GetRegs());
    if (startValid)
        m_localSettings.m_startAddress = addr;

    sizeValid = StringParsers::ParseCpuExpression(m_pLineEditSize->text().toStdString().c_str(), size,
                                       m_pTargetModel->GetSymbolTable(), m_pTargetModel->GetRegs());

    if (sizeValid)
        m_localSettings.m_sizeInBytes = size;

    if (m_localSettings.m_sizeInBytes == 0)
        valid = startValid = sizeValid = false;

    // Filename
    if (m_pFilenameTextEdit->text().size() != 0)
    {
        m_localSettings.m_filename = m_pFilenameTextEdit->text();
        fileValid = true;
    }

    valid &= startValid;
    valid &= sizeValid;
    valid &= fileValid;

    // Apply to the UI
    m_pOkButton->setEnabled(valid);
    Colouring::SetErrorState(m_pLineEditStart, startValid);
    Colouring::SetErrorState(m_pLineEditSize, sizeValid);
    Colouring::SetErrorState(m_pFilenameTextEdit, fileValid);
    return valid;
}
