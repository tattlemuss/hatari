#include "session.h"
#include <QtNetwork>
#include <QTimer>
#include <QFontDatabase>

#include "targetmodel.h"
#include "../transport/dispatcher.h"

Session::Session() :
    QObject(),
    m_autoConnect(true)
{
    m_pStartupFile = new QTemporaryFile(this);
    m_pLoggingFile = new QTemporaryFile(this);

    // Create the core data models, since other object want to connect to them.
    m_pTcpSocket = new QTcpSocket();

    m_pTargetModel = new TargetModel();
    m_pDispatcher = new Dispatcher(m_pTcpSocket, m_pTargetModel);

    m_pTimer = new QTimer(this);
    connect(m_pTimer, &QTimer::timeout, this, &Session::connectTimerCallback);

    m_pTimer->start(500);

    // Default settings
    m_settings.m_bSquarePixels = false;
    m_settings.m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    loadSettings();
}

Session::~Session()
{
    saveSettings();
    m_pLoggingFile->close();
    delete m_pTcpSocket;
    delete m_pTimer;
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

void Session::SetSettings(const Session::Settings& newSettings)
{
    m_settings = newSettings;
    emit settingsChanged();
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
    settings.endGroup();
}

void Session::saveSettings()
{
    QSettings settings;
    settings.beginGroup("Session");
    settings.setValue("font", m_settings.m_font.toString());
    settings.setValue("squarePixels", m_settings.m_bSquarePixels);
    settings.endGroup();
}

void Session::connectTimerCallback()
{
    if (m_autoConnect && m_pTcpSocket->state() == QAbstractSocket::UnconnectedState)
    {
        QHostAddress qha(QHostAddress::LocalHost);
        m_pTcpSocket->connectToHost(qha, 56001);
    }
}

