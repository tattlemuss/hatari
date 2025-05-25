#include "exceptiondialog.h"
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QGridLayout>

#include "../models/targetmodel.h"
#include "../transport/dispatcher.h"

ExceptionsGroupBox::ExceptionsGroupBox(QString title, QWidget* parent) :
    QGroupBox(title, parent)
{
    // Options grid box
    // Col    Mean
    //  0     stretch
    //  1     exceptions pt 1
    //  2     exceptions pt 2
    //  3     "all"/"non"
    //  4     stretch

    QGridLayout *pGridLayout = new QGridLayout;
    uint32_t half = (ExceptionMask::kExceptionCount + 1) / 2;
    for (uint32_t i = 0; i < ExceptionMask::kExceptionCount; ++i)
    {
        m_pCheckboxes[i] = new QCheckBox(ExceptionMask::GetName(ExceptionMask::Type(i)), this);
        pGridLayout->addWidget(m_pCheckboxes[i], i % half, 1 + i / half);
    }

    m_pSelectAllButton = new QPushButton("All", this);
    m_pSelectNoneButton = new QPushButton("None", this);
    pGridLayout->addWidget(m_pSelectAllButton, 0, 3);
    pGridLayout->addWidget(m_pSelectNoneButton, 1, 3);
    pGridLayout->setColumnStretch(0, 100);
    pGridLayout->setColumnStretch(4, 100);
    setLayout(pGridLayout);

    connect(m_pSelectAllButton, &QPushButton::clicked, this, &ExceptionsGroupBox::selectAllClicked);
    connect(m_pSelectNoneButton, &QPushButton::clicked, this, &ExceptionsGroupBox::selectNoneClicked);
}

void ExceptionsGroupBox::Set(ExceptionMask::Type type, bool enabled)
{
    m_pCheckboxes[type]->setChecked(enabled);
}

bool ExceptionsGroupBox::Get(ExceptionMask::Type type) const
{
    return m_pCheckboxes[type]->isChecked();
}

void ExceptionsGroupBox::selectAllClicked()
{
    for (uint32_t i = 0; i < ExceptionMask::kExceptionCount; ++i)
        m_pCheckboxes[i]->setChecked(true);
}

void ExceptionsGroupBox::selectNoneClicked()
{
    for (uint32_t i = 0; i < ExceptionMask::kExceptionCount; ++i)
        m_pCheckboxes[i]->setChecked(false);
}

ExceptionDialog::ExceptionDialog(QWidget *parent, TargetModel* pTargetModel, Dispatcher* pDispatcher) :
    QDialog(parent),
    m_pTargetModel(pTargetModel),
    m_pDispatcher(pDispatcher)
{
    this->setWindowTitle(tr("Set Enabled Exceptions"));

    QPushButton* pOkButton = new QPushButton("&OK", this);
    pOkButton->setDefault(true);
    QPushButton* pCancelButton = new QPushButton("&Cancel", this);


    QHBoxLayout* pHLayout = new QHBoxLayout(this);
    pHLayout->addWidget(pOkButton);
    pHLayout->addWidget(pCancelButton);
    QWidget* pButtonContainer = new QWidget(this);
    pButtonContainer->setLayout(pHLayout);

    m_pGroupBox = new ExceptionsGroupBox("Exception Types", this);

    QVBoxLayout* pWholeLayout = new QVBoxLayout(this);
    pWholeLayout->addWidget(m_pGroupBox);
    pWholeLayout->addWidget(pButtonContainer);

    connect(pOkButton, &QPushButton::clicked, this, &ExceptionDialog::okClicked);
    connect(pOkButton, &QPushButton::clicked, this, &ExceptionDialog::accept);
    connect(pCancelButton, &QPushButton::clicked, this, &ExceptionDialog::reject);
    this->setLayout(pWholeLayout);
}

ExceptionDialog::~ExceptionDialog()
{

}

void ExceptionDialog::showEvent(QShowEvent *event)
{
    const ExceptionMask& mask = m_pTargetModel->GetExceptionMask();
    for (uint32_t i = 0; i < ExceptionMask::kExceptionCount; ++i)
    {
        ExceptionMask::Type t = (ExceptionMask::Type)i;
        m_pGroupBox->Set(t, mask.Get(t));
    }

    QDialog::showEvent(event);
}

void ExceptionDialog::okClicked()
{
    ExceptionMask mask;
    for (uint32_t i = 0; i < ExceptionMask::kExceptionCount; ++i)
    {
        ExceptionMask::Type t = (ExceptionMask::Type)i;
        mask.Set(t, m_pGroupBox->Get(t));
    }

    // Send to target
    // NOTE: sending this returns a response with the set exmask,
    // so update in the target model is automatic.
    m_pDispatcher->SetExceptionMask(mask.GetAsHatari());
}


