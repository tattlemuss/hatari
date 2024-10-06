#include "searchdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
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

bool SearchSettings::CalcValues()
{
    m_masksAndValues.clear();
    if (m_mode == Mode::kText)
    {
        uint8_t caseBit = 'a' ^ 'A';       // bit which differs between two csaes
        uint8_t caseMask = 0xff ^ caseBit; // mask of this bit
        for (int i = 0; i < m_originalText.size(); ++i)
        {
            char val = m_originalText[i].toLatin1();
            uint8_t mask = 0xff;
            if (!m_matchCase && StringParsers::IsAlpha(val))
            {
                mask = caseMask;
                val &= caseMask; // remove upper bit if it exists
            }
            m_masksAndValues.push_back(mask);
            m_masksAndValues.push_back(val);
        }
        return true;
    }
    else if (m_mode == Mode::kHex)
    {
        // Parse as hex
        QVector<uint8_t> tmp;
        if (!StringParsers::ParseHexString(m_originalText.toLatin1(), tmp))
            return false;
        for (int i = 0; i < tmp.size(); ++i)
        {
            m_masksAndValues.push_back(0xff);
            m_masksAndValues.push_back(tmp[i]);
        }
        return true;
    }
    return false;
}

SearchDialog::SearchDialog(QWidget *parent, const TargetModel* pTargetModel, SearchSettings& returnedSettings) :
    QDialog(parent),
    m_pTargetModel(pTargetModel),
    m_returnedSettings(returnedSettings)
{
    this->setWindowTitle(tr("Find..."));
    this->setObjectName("SearchDialog");

    // Take a copy of the input settings
    m_localSettings = returnedSettings;

    SymbolTableModel* pSymbolTableModel = new SymbolTableModel(this, m_pTargetModel->GetSymbolTable());
    QCompleter* pCompl = new QCompleter(pSymbolTableModel, this);
    pCompl->setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);

    // Widgets.
    m_pModeCombo = new QComboBox(this);
    m_pModeCombo->insertItem(SearchSettings::kText, "Text");
    m_pModeCombo->insertItem(SearchSettings::kHex, "Hex");
    m_pModeCombo->setCurrentIndex(m_localSettings.m_mode);

    m_pLineEditString = new QLineEdit(this);
    m_pLineEditString->setText(m_localSettings.m_originalText);
    m_pMatchCaseCheckbox = new QCheckBox("Match Case", this);
    m_pMatchCaseCheckbox->setChecked(m_localSettings.m_matchCase);

    m_pLineEditStart = new QLineEdit(this);
    m_pLineEditStart->setText(Format::to_hex32(m_localSettings.m_startAddress));
    m_pLineEditStart->setCompleter(pCompl);
    m_pLineEditEnd = new QLineEdit(this);
    m_pLineEditEnd->setText(Format::to_hex32(m_localSettings.m_endAddress));
    m_pLineEditEnd->setCompleter(pCompl);

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
    gridLayout->addWidget(m_pModeCombo, row, 0);
    gridLayout->addWidget(m_pLineEditString, row, 1);
    ++row;
    gridLayout->addWidget(m_pMatchCaseCheckbox, row, 1);
    ++row;
    gridLayout->addWidget(new QLabel(tr("Start:")), row, 0);
    gridLayout->addWidget(m_pLineEditStart, row, 1);
    ++row;
    gridLayout->addWidget(new QLabel(tr("End:")), row, 0);
    gridLayout->addWidget(m_pLineEditEnd, row, 1);
    ++row;

    // Layout grid
    gridLayout->setColumnStretch(2, 20);
    gridGroupBox->setLayout(gridLayout);

    // Overall layout (options at top, buttons at bottom)
    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(gridGroupBox);
    pLayout->addWidget(pButtonContainer);
    m_pLineEditString->setFocus();

    CheckInputs();

    connect(m_pLineEditStart,     &QLineEdit::textChanged,        this, &SearchDialog::textEditChanged);
    connect(m_pLineEditEnd,       &QLineEdit::textChanged,        this, &SearchDialog::textEditChanged);
    connect(m_pLineEditString,    &QLineEdit::textChanged,        this, &SearchDialog::textEditChanged);

    connect(m_pModeCombo, SIGNAL(activated(int)), SLOT(modeChangedSlot(int)));  // this is user-changed
    connect(m_pMatchCaseCheckbox, &QCheckBox::toggled,            this, &SearchDialog::matchCaseChanged);
    connect(m_pOkButton, &QPushButton::clicked, this, &SearchDialog::okClicked);
    connect(m_pOkButton, &QPushButton::clicked, this, &SearchDialog::accept);
    connect(pCancelButton, &QPushButton::clicked, this, &SearchDialog::reject);
}

void SearchDialog::showEvent(QShowEvent *event)
{

    QDialog::showEvent(event);
}

void SearchDialog::okClicked()
{
    if (!CheckInputs())
        return;

    // Generate the string in local settings
    m_localSettings.m_originalText = m_pLineEditString->text();
    m_localSettings.CalcValues();
    m_returnedSettings = m_localSettings;
}

void SearchDialog::textEditChanged()
{
    CheckInputs();
}

void SearchDialog::matchCaseChanged()
{
    m_localSettings.m_matchCase = m_pMatchCaseCheckbox->isChecked();
    CheckInputs();
}

void SearchDialog::modeChangedSlot(int index)
{
    m_localSettings.m_mode = (SearchSettings::Mode) index;
    CheckInputs();
}

bool SearchDialog::CheckInputs()
{
    bool valid = true;
    bool startValid = true;
    bool endValid = true;
    bool textValid = true;

    m_localSettings.m_originalText = m_pLineEditString->text();
    if (!m_localSettings.CalcValues())
        textValid = false;

    if (m_localSettings.m_masksAndValues.size() == 0)
        textValid = false;

    uint32_t addr;
    startValid = StringParsers::ParseCpuExpression(m_pLineEditStart->text().toStdString().c_str(), addr,
                                       m_pTargetModel->GetSymbolTable(), m_pTargetModel->GetRegs());
    if (startValid)
        m_localSettings.m_startAddress = addr;

    endValid = StringParsers::ParseCpuExpression(m_pLineEditEnd->text().toStdString().c_str(), addr,
                                       m_pTargetModel->GetSymbolTable(), m_pTargetModel->GetRegs());

    if (endValid)
        m_localSettings.m_endAddress = addr;

    if (m_localSettings.m_startAddress >= m_localSettings.m_endAddress)
        valid = startValid = endValid = false;

    valid &= textValid;
    valid &= startValid;
    valid &= endValid;

    // Apply to the UI
    m_pMatchCaseCheckbox->setEnabled(m_localSettings.m_mode == SearchSettings::kText);
    m_pOkButton->setEnabled(valid);
    Colouring::SetErrorState(m_pLineEditString, textValid);
    Colouring::SetErrorState(m_pLineEditStart, startValid);
    Colouring::SetErrorState(m_pLineEditEnd, endValid);
    return valid;
}
