#include "rundialog.h"
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QProcess>

#include "../models/targetmodel.h"
#include "../transport/dispatcher.h"
#include "quicklayout.h"

RunDialog::RunDialog(QWidget *parent, TargetModel* pTargetModel, Dispatcher* pDispatcher) :
    QDialog(parent),
    m_pTargetModel(pTargetModel),
    m_pDispatcher(pDispatcher)
{
    this->setWindowTitle(tr("Run Hatari"));

    QPushButton* pOkButton = new QPushButton("&OK", this);
    pOkButton->setDefault(true);
    QPushButton* pCancelButton = new QPushButton("&Cancel", this);

    QHBoxLayout* pHLayout = new QHBoxLayout(this);
    pHLayout->addWidget(pOkButton);
    pHLayout->addWidget(pCancelButton);
    QWidget* pButtonContainer = new QWidget(this);
    pButtonContainer->setLayout(pHLayout);

    QWidget* list1, * list2;

    {
        m_pExecutableTextEdit = new QLineEdit("hatari", this);
        QLabel* pFront = new QLabel("Executable", this);

        QWidget* list[] = { pFront, m_pExecutableTextEdit, nullptr};
        list1 = CreateHorizLayout(this, list);
    }

    {
        m_pArgsTextEdit = new QLineEdit("", this);
        QLabel* pFront = new QLabel("Arguments", this);

        QWidget* list[] = { pFront, m_pArgsTextEdit, nullptr };
        list2 = CreateHorizLayout(this, list);
    }

    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(list1);
    pLayout->addWidget(list2);
    pLayout->addWidget(pButtonContainer);

    connect(pOkButton, &QPushButton::clicked, this, &RunDialog::okClicked);
    connect(pOkButton, &QPushButton::clicked, this, &RunDialog::accept);

    connect(pCancelButton, &QPushButton::clicked, this, &RunDialog::reject);
    this->setLayout(pLayout);
}

RunDialog::~RunDialog()
{

}

void RunDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
}

void RunDialog::okClicked()
{
    QProcess proc;
    QStringList args = m_pArgsTextEdit->text().split(" ");

    proc.setProgram(m_pExecutableTextEdit->text());
    proc.setArguments(args);
    proc.startDetached();
}
