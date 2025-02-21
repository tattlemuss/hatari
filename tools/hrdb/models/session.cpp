#include "session.h"
#include <QtNetwork>
#include <QTimer>
#include <QFontDatabase>

#include "../transport/dispatcher.h"
#include "targetmodel.h"
#include "filewatcher.h"
#include "programdatabase.h"

Session::Session() :
    QObject(),
    m_pFileWatcher(nullptr),
    m_autoConnect(true)
{
    m_pStartupFile = new QTemporaryFile(this);
    m_pProgramStartScript = new QTemporaryFile(this);

    m_pLoggingFile = new QTemporaryFile(this);

    // Create the core data models, since other object want to connect to them.
    m_pTcpSocket = new QTcpSocket();

    m_pTargetModel = new TargetModel();
    m_pDispatcher = new Dispatcher(m_pTcpSocket, m_pTargetModel);

    m_pProgramDatabase = new ProgramDatabase(this);

    m_pTimer = new QTimer(this);
    connect(m_pTimer, &QTimer::timeout, this, &Session::connectTimerCallback);

    m_pTimer->start(500);

    // Default settings
    m_settings.m_bSquarePixels = false;
    m_settings.m_bDisassHexNumerics = false;
    m_settings.m_profileDisplayMode = Settings::kTotal;
    m_settings.m_liveRefresh = false;
    m_settings.m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    // Watch for certain things changing on the target
    connect(m_pTargetModel, &TargetModel::programPathChangedSignal, this, &Session::programPathChanged);
    loadSettings();
}

Session::~Session()
{
    saveSettings();
    m_pLoggingFile->close();
    delete m_pTcpSocket;
    delete m_pTimer;
    delete m_pFileWatcher;
    if (m_pHatariProcess)
    {
        m_pHatariProcess->detach();
        delete m_pHatariProcess;
    }
}

void Session::Connect()
{
    m_autoConnect = true;
    // Have a first go immediately, just in case
    connectTimerCallback();
}

void Session::Disconnect()
{
    m_autoConnect = false;
    m_pTcpSocket->disconnectFromHost();
}

const Session::Settings &Session::GetSettings() const
{
    return m_settings;
}

const LaunchSettings &Session::GetLaunchSettings() const
{
    return m_launchSettings;
}

void Session::SetSettings(const Session::Settings& newSettings)
{
    m_settings = newSettings;
    emit settingsChanged();
}

void Session::SetLaunchSettings(const LaunchSettings& newSettings)
{
    m_launchSettings = newSettings;
    emit settingsChanged();
}

void Session::SetMessage(const QString &msg)
{
    emit messageSet(msg);
}

void Session::loadSettings()
{
    QSettings settings;
    settings.beginGroup("Session");
    if (settings.contains("font"))
    {
        QString fontString = settings.value("font").toString();
        m_settings.m_font.fromString(fontString);
    }
    m_settings.m_bSquarePixels = settings.value("squarePixels", QVariant(false)).toBool();
    m_settings.m_bDisassHexNumerics = settings.value("disassHexNumerics", QVariant(false)).toBool();
    m_settings.m_liveRefresh = settings.value("liveRefresh", QVariant(false)).toBool();
    m_settings.m_profileDisplayMode = settings.value("profileDisplayMode", QVariant(Settings::kTotal)).toInt();
    settings.endGroup();

    m_launchSettings.loadSettings(settings);
}

void Session::saveSettings()
{
    QSettings settings;
    settings.beginGroup("Session");
    settings.setValue("font", m_settings.m_font.toString());
    settings.setValue("squarePixels", m_settings.m_bSquarePixels);
    settings.setValue("disassHexNumerics", m_settings.m_bDisassHexNumerics);
    settings.setValue("liveRefresh", m_settings.m_liveRefresh);
    settings.setValue("profileDisplayMode", m_settings.m_profileDisplayMode);
    settings.endGroup();

    m_launchSettings.saveSettings(settings);
}

void Session::connectTimerCallback()
{
    if (m_autoConnect && m_pTcpSocket->state() == QAbstractSocket::UnconnectedState)
    {
        QHostAddress qha(QHostAddress::LocalHost);
        m_pTcpSocket->connectToHost(qha, 56001);
    }
}

void Session::programPathChanged()
{
    std::string progPath = m_pTargetModel->GetProgramPath();
    if (progPath.size() != 0)
    {
        SetMessage(QString("New program: ") + QString::fromStdString(progPath));
        m_pProgramDatabase->SetPath(progPath);
    }
    else
    {
        SetMessage("Program unloaded.");
        m_pProgramDatabase->Clear();
    }
}

void Session::resetWarm()
{
    m_pDispatcher->ResetWarm();

    // This will re-request from Hatari, which should return
    // an empty symbol table.
    m_pDispatcher->ReadSymbols();

    // Restart if in break mode
    if (!m_pTargetModel->IsRunning())
        m_pDispatcher->Run();
}

void Session::resetCold()
{
    m_pDispatcher->ResetCold();

    // This will re-request from Hatari, which should return
    // an empty symbol table.
    m_pDispatcher->ReadSymbols();

    // Restart if in break mode
    if (!m_pTargetModel->IsRunning())
        m_pDispatcher->Run();
}

FileWatcher* Session::createFileWatcherInstance()
{
        if (!m_pFileWatcher)
        {
            m_pFileWatcher=new FileWatcher(this);
        }
        return m_pFileWatcher;
}

void Session::setHatariProcess(DetachableProcess* pProc)
{
    if (m_pHatariProcess)
    {
        // Kill any old Hatari process which might be active.
        if (m_pHatariProcess->state() == QProcess::Running)
            m_pHatariProcess->terminate();
        delete m_pHatariProcess;
    }
    m_pHatariProcess = pProc;
}
