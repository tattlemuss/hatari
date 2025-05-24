#include "exceptiondialog.h"
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "../models/targetmodel.h"
#include "../transport/dispatcher.h"

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

    QVBoxLayout* pLayout = new QVBoxLayout(this);
    for (uint32_t i = 0; i < ExceptionMask::kExceptionCount; ++i)
    {
        m_pCheckboxes[i] = new QCheckBox(ExceptionMask::GetName(ExceptionMask::Type(i)), this);
        pLayout->addWidget(m_pCheckboxes[i]);
    }
    pLayout->addWidget(pButtonContainer);

    connect(pOkButton, &QPushButton::clicked, this, &ExceptionDialog::okClicked);

    connect(pOkButton, &QPushButton::clicked, this, &ExceptionDialog::accept);
    connect(pCancelButton, &QPushButton::clicked, this, &ExceptionDialog::reject);
    this->setLayout(pLayout);
}

ExceptionDialog::~ExceptionDialog()
{

}

void ExceptionDialog::showEvent(QShowEvent *event)
{
    const ExceptionMask& mask = m_pTargetModel->GetExceptionMask();
    for (uint32_t i = 0; i < ExceptionMask::kExceptionCount; ++i)
        m_pCheckboxes[i]->setChecked(mask.Get(ExceptionMask::Type(i)));

    QDialog::showEvent(event);
}

void ExceptionDialog::okClicked()
{
    ExceptionMask mask;
    for (uint32_t i = 0; i < ExceptionMask::kExceptionCount; ++i)
        if (m_pCheckboxes[i]->isChecked()) mask.Set(ExceptionMask::Type(i));

    // Send to target
    // NOTE: sending this returns a response with the set exmask,
    // so update in the target model is automatic.
    m_pDispatcher->SetExceptionMask(mask.GetAsHatari());
}
