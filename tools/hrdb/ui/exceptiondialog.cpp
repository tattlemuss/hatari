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
    for (int i = 0; i < ExceptionMask::kExceptionCount; ++i)
    {
        m_pCheckboxes[i] = new QCheckBox(ExceptionMask::GetName(i + 2), this);
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
    m_pCheckboxes[0]->setChecked(mask.Get(ExceptionMask::kBus));
    m_pCheckboxes[1]->setChecked(mask.Get(ExceptionMask::kAddress));
    m_pCheckboxes[2]->setChecked(mask.Get(ExceptionMask::kIllegal));
    m_pCheckboxes[3]->setChecked(mask.Get(ExceptionMask::kZeroDiv));
    m_pCheckboxes[4]->setChecked(mask.Get(ExceptionMask::kChk));
    m_pCheckboxes[5]->setChecked(mask.Get(ExceptionMask::kTrapv));
    m_pCheckboxes[6]->setChecked(mask.Get(ExceptionMask::kPrivilege));
    m_pCheckboxes[7]->setChecked(mask.Get(ExceptionMask::kTrace));

    QDialog::showEvent(event);
}

void ExceptionDialog::okClicked()
{
    ExceptionMask mask;
    mask.m_mask = 0;

    if (m_pCheckboxes[0]->isChecked()) mask.Set(ExceptionMask::kBus);
    if (m_pCheckboxes[1]->isChecked()) mask.Set(ExceptionMask::kAddress);
    if (m_pCheckboxes[2]->isChecked()) mask.Set(ExceptionMask::kIllegal);
    if (m_pCheckboxes[3]->isChecked()) mask.Set(ExceptionMask::kZeroDiv);
    if (m_pCheckboxes[4]->isChecked()) mask.Set(ExceptionMask::kChk);
    if (m_pCheckboxes[5]->isChecked()) mask.Set(ExceptionMask::kTrapv);
    if (m_pCheckboxes[6]->isChecked()) mask.Set(ExceptionMask::kPrivilege);
    if (m_pCheckboxes[7]->isChecked()) mask.Set(ExceptionMask::kTrace);

    // Send to target
    // NOTE: sending this returns a response with the set exmask,
    // so update in the target model is automatic.
    m_pDispatcher->SendCommandPacket(QString::asprintf("exmask %u", mask.m_mask).toStdString().c_str());
}
