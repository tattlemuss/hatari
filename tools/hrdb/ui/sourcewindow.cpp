#include "sourcewindow.h"

#include <iostream>
#include <QDebug>
#include <QTextEdit>
#include <QSettings>
#include <QVBoxLayout>

#include <QFile>
#include <QTextBlock>

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../models/session.h"
#include "../models/programdatabase.h"
#include "quicklayout.h"

//-----------------------------------------------------------------------------
class SourceFile
{
public:
    // ...
    QString key;
    QString text;
};

//-----------------------------------------------------------------------------
class SourceCache
{
public:
    SourceCache();
    ~SourceCache();
    bool GetFile(const std::string& dir, const std::string& file, const SourceFile*& pFileData);

    static QString GetKey(const std::string& dir, const std::string& file);

    typedef std::map<QString, SourceFile*> Map;

    Map m_fileMap;
};

SourceCache::SourceCache()
{
}

SourceCache::~SourceCache()
{
    SourceCache::Map::iterator it = m_fileMap.begin();
    while (it != m_fileMap.end())
    {
        delete it->second;
        ++it;
    }
}

bool SourceCache::GetFile(const std::string& dir, const std::string& file,
                          const SourceFile*& pFileData)
{
    /*
     * "Qt uses "/" as a universal directory separator in the same way that "/" is used as a
     * path separator in URLs. If you always use "/" as a directory separator, Qt will translate
     * your paths to conform to the underlying operating system.
     */

    pFileData = nullptr;
    QString fileKey = GetKey(dir, file);

    const SourceCache::Map::iterator findIt = m_fileMap.find(fileKey);
    if (findIt != m_fileMap.end())
    {
        pFileData = findIt->second;
        return true;
    }

    QString basePath("/home/steve/projects/atari/bigbrownbuild-git/barebones/");

    QFile testFile(basePath + fileKey);
    if (!testFile.exists())
        return false;

    // Load the file in and return it
    testFile.open(QFile::ReadOnly);

    QTextStream in(&testFile);
    QString text = in.readAll();

    SourceFile* pNewFile = new SourceFile();
    pNewFile->key = fileKey;
    pNewFile->text = text;

    m_fileMap.insert(std::make_pair(fileKey, pNewFile));

    pFileData = pNewFile;
    return true;
}

QString SourceCache::GetKey(const std::string& dir, const std::string& file)
{
    QString fileKey = QString::asprintf("%s/%s", dir.c_str(), file.c_str());
    return fileKey;
}

//-----------------------------------------------------------------------------
SourceWindow::SourceWindow(QWidget *parent, Session* pSession) :
    QDockWidget(parent),
    m_pSession(pSession),
    m_pTargetModel(pSession->m_pTargetModel),
    m_pDispatcher(pSession->m_pDispatcher)
{
    this->setWindowTitle("Source");
    setObjectName("Source");

    m_pSourceCache = new SourceCache();

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
    connect(m_pTargetModel,     &TargetModel::mainStateCompletedSignal,   this, &SourceWindow::mainStateCompleted);

    connect(m_pSession,         &Session::settingsChanged,              this, &SourceWindow::settingsChanged);

    // Refresh enable state
    connectChanged();

    // Refresh font
    settingsChanged();
}

SourceWindow::~SourceWindow()
{
    delete m_pSourceCache;
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

void SourceWindow::startStopDelayed(int /*running*/)
{
}

void SourceWindow::mainStateCompleted()
{
    uint32_t textAddr = m_pTargetModel->GetRegs().Get(Registers::TEXT);
    uint32_t dataAddr = m_pTargetModel->GetRegs().Get(Registers::DATA);
    uint32_t pc = m_pTargetModel->GetStartStopPC(kProcCpu);
    uint32_t textOffset = pc - textAddr;
    ProgramDatabase::CodeInfo info;
    QString txt = "unknown";

    if (pc < dataAddr && m_pSession->m_pProgramDatabase->FindLowerOrEqual(textOffset, info))
    {
        txt = QString::asprintf("%x map to > %x %s/%s %d %d\n",
                                               pc,
                                               info.m_address + textAddr,
                                               info.m_dir.c_str(),
                                               info.m_file.c_str(),
                                               info.m_line,
                                               info.m_column);

        const SourceFile* pFileData;
        QString key = SourceCache::GetKey(info.m_dir, info.m_file);
        if (key != m_currFileKey)
        {
            if (!m_pSourceCache->GetFile(info.m_dir, info.m_file, pFileData))
                return;

            m_pSourceTextEdit->setPlainText(pFileData->text);
            m_currFileKey = key;
        }
        // This is utter garbage, so I need a better approach
        QTextCursor textCursor = m_pSourceTextEdit->textCursor();
        QTextBlock b = textCursor.document()->findBlockByLineNumber(info.m_line - 1);
        textCursor.setPosition(b.position() + 0, QTextCursor::MoveAnchor);
        m_pSourceTextEdit->setTextCursor(textCursor);
        m_pSourceTextEdit->moveCursor(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        return;
    }
    m_pSourceTextEdit->setText(txt);
}

void SourceWindow::settingsChanged()
{
//    QFontMetrics fm(m_pSession->GetSettings().m_font);

    // Down the side
//    m_pTreeView->setFont(m_pSession->GetSettings().m_font);
    loadSettings();
}

