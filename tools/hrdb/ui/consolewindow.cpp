#include "consolewindow.h"

#include <iostream>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QSettings>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTextEdit>
#include <QDebug>
#include <QFileSystemWatcher>

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../models/session.h"
#include "quicklayout.h"

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
ConsoleWindow::ConsoleWindow(QWidget *parent, Session* pSession) :
    QDockWidget(parent),
    m_pSession(pSession),
    m_pTargetModel(pSession->m_pTargetModel),
    m_pDispatcher(pSession->m_pDispatcher),
    m_pWatcher(nullptr)
{
    this->setWindowTitle("Console");
    setObjectName("Console");

    m_pLineEdit = new QLineEdit(this);
    m_pTextArea = new QTextEdit(this);
    m_pTextArea->setReadOnly(true);

    // Layouts
    QVBoxLayout* pMainLayout = new QVBoxLayout;
    QHBoxLayout* pTopLayout = new QHBoxLayout;
    auto pMainRegion = new QWidget(this);   // whole panel
    auto pTopRegion = new QWidget(this);      // top buttons/edits

    SetMargins(pTopLayout);
    pTopLayout->addWidget(m_pLineEdit);
    SetMargins(pMainLayout);
    pMainLayout->addWidget(pTopRegion);
    pMainLayout->addWidget(m_pTextArea);

    pTopRegion->setLayout(pTopLayout);
    pMainRegion->setLayout(pMainLayout);
    setWidget(pMainRegion);

    loadSettings();

    connect(m_pTargetModel,  &TargetModel::connectChangedSignal, this, &ConsoleWindow::connectChanged);
    connect(m_pSession,      &Session::settingsChanged,          this, &ConsoleWindow::settingsChanged);
    // Connect text entry
    connect(m_pLineEdit,     &QLineEdit::returnPressed,          this, &ConsoleWindow::textEditChanged);

    // Refresh enable state
    connectChanged();

    // Refresh font
    settingsChanged();
}

ConsoleWindow::~ConsoleWindow()
{
    deleteWatcher();
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

void ConsoleWindow::connectChanged()
{
    bool enable = m_pTargetModel->IsConnected();
    m_pLineEdit->setEnabled(enable);

    if (m_pTargetModel->IsConnected())
    {
        // Experimental: force Hatari output to a file
        if (m_pTargetModel->IsConnected())
        {
            deleteWatcher();

            // Open the temp file
            m_pSession->m_pLoggingFile->open();
            QString filename = m_pSession->m_pLoggingFile->fileName();

            // Need to open a file watcher here
            m_pWatcher = new QFileSystemWatcher(this);
            m_pWatcher->addPath(m_pSession->m_pLoggingFile->fileName());
            connect(m_pWatcher, &QFileSystemWatcher::fileChanged,   this, &ConsoleWindow::fileChanged);

            // Create a reader
            m_tempFile.setFileName(filename);
            m_tempFile.open(QIODevice::ReadOnly| QIODevice::Unbuffered);
            m_tempFileTextStream.setDevice(&m_tempFile);

            m_pDispatcher->SetLoggingFile(filename.toStdString());
        }
    }
    else
    {
        deleteWatcher();
    }
}

void ConsoleWindow::settingsChanged()
{
    m_pTextArea->setFont(m_pSession->GetSettings().m_font);
}

void ConsoleWindow::textEditChanged()
{
    if (m_pTargetModel->IsConnected() && !m_pTargetModel->IsRunning())
    {
        m_pDispatcher->SendConsoleCommand(m_pLineEdit->text().toStdString());
        m_pTextArea->append(QString(">>") + m_pLineEdit->text());
    }
    m_pLineEdit->clear();
}

void ConsoleWindow::fileChanged(const QString& filename)
{
    Q_ASSERT(filename == m_tempFile.fileName());

    // Read whatever possible from file
    QString data;
    data = m_tempFileTextStream.readLine();
    while (!data.isNull())
    {
        m_pTextArea->append(data);
        data = m_tempFileTextStream.readLine();
    }
}

void ConsoleWindow::deleteWatcher()
{
    if (m_pWatcher)
    {
        m_tempFile.close();
        disconnect(m_pWatcher, &QFileSystemWatcher::fileChanged,   this, &ConsoleWindow::fileChanged);
        delete m_pWatcher;
        m_pWatcher = nullptr;
    }
}
