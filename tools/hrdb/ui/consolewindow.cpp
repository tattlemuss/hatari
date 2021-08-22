#include "consolewindow.h"

#include <iostream>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QSettings>

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "quicklayout.h"

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
ConsoleWindow::ConsoleWindow(QWidget *parent, TargetModel* pTargetModel, Dispatcher* pDispatcher) :
    QDockWidget(parent),
    m_pTargetModel(pTargetModel),
    m_pDispatcher(pDispatcher)
{
    this->setWindowTitle("Console");
    setObjectName("Console");

    m_pLineEdit = new QLineEdit(this);

    // Layouts
    QVBoxLayout* pMainLayout = new QVBoxLayout;
    QHBoxLayout* pTopLayout = new QHBoxLayout;
    auto pMainRegion = new QWidget(this);   // whole panel
    auto pTopRegion = new QWidget(this);      // top buttons/edits

    SetMargins(pTopLayout);
    pTopLayout->addWidget(m_pLineEdit);
    SetMargins(pMainLayout);
    pMainLayout->addWidget(pTopRegion);

    pTopRegion->setLayout(pTopLayout);
    pMainRegion->setLayout(pMainLayout);
    setWidget(pMainRegion);

    loadSettings();

    connect(m_pTargetModel,  &TargetModel::connectChangedSignal, this, &ConsoleWindow::connectChangedSlot);
    // Connect text entry
    connect(m_pLineEdit,     &QLineEdit::returnPressed,          this, &ConsoleWindow::textEditChangedSlot);

    // Refresh enable state
    connectChangedSlot();
}

void ConsoleWindow::keyFocus()
{
    activateWindow();
    m_pLineEdit->setFocus();
}

void ConsoleWindow::loadSettings()
{
    QSettings settings;
    settings.beginGroup("Console");

    restoreGeometry(settings.value("geometry").toByteArray());
    settings.endGroup();
}

void ConsoleWindow::saveSettings()
{
    QSettings settings;
    settings.beginGroup("Console");

    settings.setValue("geometry", saveGeometry());
    settings.endGroup();
}

void ConsoleWindow::connectChangedSlot()
{
    bool enable = m_pTargetModel->IsConnected();
    m_pLineEdit->setEnabled(enable);
}

void ConsoleWindow::textEditChangedSlot()
{
    if (m_pTargetModel->IsConnected() && !m_pTargetModel->IsRunning())
    {
        QString string = "console ";
        string += m_pLineEdit->text();
        m_pDispatcher->SendCommandPacket(string.toStdString().c_str());
    }
    m_pLineEdit->clear();
}
