#include "sourcewindow.h"

#include <iostream>
#include <QDebug>
#include <QTextEdit>
#include <QSettings>
#include <QVBoxLayout>

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../models/session.h"
#include "quicklayout.h"

//-----------------------------------------------------------------------------
SourceWindow::SourceWindow(QWidget *parent, Session* pSession) :
    QDockWidget(parent),
    m_pSession(pSession),
    m_pTargetModel(pSession->m_pTargetModel),
    m_pDispatcher(pSession->m_pDispatcher)
{
    this->setWindowTitle("Source");
    setObjectName("Source");

    m_pSourceTextEdit = new QTextEdit(this);
    m_pSourceTextEdit->setReadOnly(true);
    m_pSourceTextEdit->clear();
    m_pSourceTextEdit->insertPlainText("Source goes here.");

    // Layouts
    QVBoxLayout* pMainLayout = new QVBoxLayout;
    QHBoxLayout* pTopLayout = new QHBoxLayout;
    auto pMainRegion = new QWidget(this);   // whole panel
    auto pTopRegion = new QWidget(this);      // top buttons/edits
    pTopLayout->addStretch();

    pMainLayout->addWidget(pTopRegion);
    pMainLayout->addWidget(m_pSourceTextEdit);

    SetMargins(pTopLayout);
    SetMargins(pMainLayout);

    pTopRegion->setLayout(pTopLayout);
    pMainRegion->setLayout(pMainLayout);
    setWidget(pMainRegion);

    loadSettings();

    connect(m_pTargetModel,     &TargetModel::connectChangedSignal,     this, &SourceWindow::connectChanged);
    connect(m_pTargetModel,     &TargetModel::startStopChangedSignal,   this, &SourceWindow::startStopChanged);
    connect(m_pTargetModel,     &TargetModel::startStopChangedSignalDelayed,   this, &SourceWindow::startStopDelayed);
    connect(m_pSession,         &Session::settingsChanged,              this, &SourceWindow::settingsChanged);

    // Refresh enable state
    connectChanged();

    // Refresh font
    settingsChanged();
}

SourceWindow::~SourceWindow()
{
}

void SourceWindow::keyFocus()
{
    activateWindow();
}

void SourceWindow::loadSettings()
{
    QSettings settings;
    settings.beginGroup("Source");

    restoreGeometry(settings.value("geometry").toByteArray());
    settings.endGroup();

    m_pSourceTextEdit->setFont(m_pSession->GetSettings().m_font);
}

void SourceWindow::saveSettings()
{
    QSettings settings;
    settings.beginGroup("Source");

    settings.setValue("geometry", saveGeometry());
    settings.endGroup();
}

void SourceWindow::connectChanged()
{
}

void SourceWindow::startStopChanged()
{
}

void SourceWindow::startStopDelayed(int running)
{
}

void SourceWindow::settingsChanged()
{
//    QFontMetrics fm(m_pSession->GetSettings().m_font);

    // Down the side
//    m_pTreeView->setFont(m_pSession->GetSettings().m_font);
    loadSettings();
}

