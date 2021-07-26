#include "addbreakpointdialog.h"
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLabel>

#include "../models/targetmodel.h"
#include "../transport/dispatcher.h"
#include "quicklayout.h"

AddBreakpointDialog::AddBreakpointDialog(QWidget *parent, TargetModel* pTargetModel, Dispatcher* pDispatcher) :
    QDialog(parent),
    m_pTargetModel(pTargetModel),
    m_pDispatcher(pDispatcher)
{
    this->setWindowTitle(tr("Add Breakpoint"));

    QLabel* pExpLabel = new QLabel("Expression:", this);
    m_pExpressionEdit = new QLineEdit(this);

    m_pOnceCheckBox = new QCheckBox("Once", this);
    QCheckBox* m_pTraceCheckBox = new QCheckBox("Trace Only", this);

    QWidget* pRow1[] = {pExpLabel, m_pExpressionEdit, m_pOnceCheckBox, nullptr};
    QWidget* pRow2[] = {m_pOnceCheckBox, m_pTraceCheckBox, nullptr};

    QPushButton* pOkButton = new QPushButton("&OK", this);
    pOkButton->setDefault(true);
    QPushButton* pCancelButton = new QPushButton("&Cancel", this);

    QHBoxLayout* pHLayout = new QHBoxLayout(this);
    pHLayout->addWidget(pOkButton);
    pHLayout->addWidget(pCancelButton);
    QWidget* pButtonContainer = new QWidget(this);
    pButtonContainer->setLayout(pHLayout);

    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(CreateHorizLayout(this, pRow1));
    pLayout->addWidget(CreateHorizLayout(this, pRow2));
    pLayout->addWidget(pButtonContainer);

    connect(pOkButton, &QPushButton::clicked, this, &AddBreakpointDialog::okClicked);
    connect(pOkButton, &QPushButton::clicked, this, &AddBreakpointDialog::accept);
    connect(pCancelButton, &QPushButton::clicked, this, &AddBreakpointDialog::reject);
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
    // Create an expression string
    m_pDispatcher->SetBreakpoint(m_pExpressionEdit->text().toStdString(), m_pOnceCheckBox->isChecked());
}
