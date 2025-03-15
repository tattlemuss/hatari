#include "sourcewindow.h"

#include <iostream>
#include <QDebug>
#include <QTextEdit>
#include <QSettings>
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextBlock>

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../models/session.h"
#include "../models/programdatabase.h"
#include "quicklayout.h"

/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QPainter>
#include <QTextBlock>

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent),
    m_currentLine(-1)
{
    lineNumberArea = new LineNumberArea(this);

    connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLineNumberArea);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth()
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    int space = 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;

    return space;
}

void CodeEditor::updateLineNumberAreaWidth(int /* newBlockCount */)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);

    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extraSelections;
    // Use our own specified highlight line.
    if (m_currentLine >= 0) {
        QTextCursor textCursor = this->textCursor();
        QTextBlock b = textCursor.document()->findBlockByNumber(m_currentLine - 1);
        textCursor.setPosition(b.position(), QTextCursor::MoveAnchor);
        QTextEdit::ExtraSelection selection;

        QColor lineColor = QColor(Qt::yellow).lighter(160);

        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor;
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    setExtraSelections(extraSelections);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), Qt::lightGray);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(Qt::black);
            painter.drawText(0, top, lineNumberArea->width(), fontMetrics().height(),
                             Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

//-----------------------------------------------------------------------------
class SourceFile
{
public:
    // ...
    QString key;
    QString path;       // file on disk
    bool isLoaded;
    QString text;
};

//-----------------------------------------------------------------------------
class SourceCache
{
public:
    SourceCache();
    ~SourceCache();

    struct FilePath
    {
        std::string dir;
        std::string file;
    };

    struct File
    {
        QString name;
        QString key;
    };

    // Decide on the locations of all files once debug data is loaded.
    void Rescan(const QVector<FilePath>& paths, const QVector<QString> searchDirs);

    // Get the contents for the file, by key
    bool GetFile(const QString& fileKey, QString& fileData);

    static QString GetKey(const std::string& dir, const std::string& file);

    typedef std::map<QString, SourceFile> Map;
    typedef std::vector<File> Vec;

    Map m_fileMap;
    Vec m_fileVec;
};

SourceCache::SourceCache()
{
}

SourceCache::~SourceCache()
{
    SourceCache::Map::iterator it = m_fileMap.begin();
    while (it != m_fileMap.end())
    {
        //delete it->second;
        ++it;
    }
}

void SourceCache::Rescan(const QVector<FilePath>& paths, const QVector<QString> searchDirs)
{
    m_fileMap.clear();
    m_fileVec.clear();
    // Qt always uses "/" as a separator.

    QString sep("/");
    // Build up a list of files and where they live
    for (int i = 0; i < paths.size(); ++i)
    {
        std::string dir = paths[i].dir;
        std::string file = paths[i].file;
        for (int j = 0; j < searchDirs.size(); ++j)
        {
            QFile testFile(searchDirs[j] + sep + QString::fromStdString(dir) + sep + QString::fromStdString(file));
            if (testFile.exists())
            {
                QFileInfo finfo(testFile);
                // Create an entry.
                SourceFile f;
                f.key = GetKey(dir, file);
                f.isLoaded = false;
                f.path = finfo.absoluteFilePath();
                f.text = QString("");
                //qDebug() << "Found source file " << f.path;
                m_fileMap.insert(std::make_pair(f.key, f));

                // Add to the overall list
                File f2;
                f2.key = f.key;
                f2.name = f.path;
                m_fileVec.push_back(f2);
                break;
            }
        }
    }
}

bool SourceCache::GetFile(const QString& fileKey,
                          QString& fileData)
{
    fileData.clear();
    const SourceCache::Map::iterator findIt = m_fileMap.find(fileKey);
    if (findIt != m_fileMap.end())
    {
        SourceFile& sf = findIt->second;
        // Load the data if needed
        if (!sf.isLoaded)
        {
            // Load the file in and return it
            QFile testFile(sf.path);
            if (testFile.open(QFile::ReadOnly))
            {

                QTextStream in(&testFile);
                sf.text = in.readAll();
                sf.isLoaded = true;
            }
        }

        fileData = sf.text;
        return true;
    }

    return false;
}

//-----------------------------------------------------------------------------
QString SourceCache::GetKey(const std::string& dir, const std::string& file)
{
    QString fileKey = QString::asprintf("%s/%s", dir.c_str(), file.c_str());
    return fileKey;
}

//-----------------------------------------------------------------------------
// Structures symbol table for display or auto-completion
class SourceFilesModel : public QAbstractListModel
{
public:
    SourceFilesModel(QObject *parent, const SourceCache& sources);
    virtual ~SourceFilesModel() {}

    // Flag that the symbol table has changed.
    // Issue signals so that QCompleter objects don't get broken state.
    void emitChanged();

    virtual int rowCount(const QModelIndex &parent) const;
    virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
    const SourceCache& m_sources;
};

//-----------------------------------------------------------------------------
SourceFilesModel::SourceFilesModel(QObject *parent, const SourceCache &sources) :
    QAbstractListModel(parent),
    m_sources(sources)
{
}

//-----------------------------------------------------------------------------
void SourceFilesModel::emitChanged()
{
    beginResetModel();
    endResetModel();
}

//-----------------------------------------------------------------------------
int SourceFilesModel::rowCount(const QModelIndex & parent) const
{
    if (parent.isValid())
        return 0;
    return (int)m_sources.m_fileMap.size();
}

//-----------------------------------------------------------------------------
QVariant SourceFilesModel::data(const QModelIndex &index, int role) const
{
    if (role == Qt::EditRole || role == Qt::DisplayRole)
    {
        size_t row = index.row();
        if (row < m_sources.m_fileVec.size())
            return QString(m_sources.m_fileVec[row].name);
    }
    return QVariant();
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
    m_pSourceFilesModel = new SourceFilesModel(this, *m_pSourceCache);

    m_pFileSelectCombo = new QComboBox(this);
    m_pFileSelectCombo->setModel(m_pSourceFilesModel);
    m_pInfoLabel = new QLabel(this);

    m_pSourceTextEdit = new CodeEditor(this);
    m_pSourceTextEdit->setReadOnly(true);
    m_pSourceTextEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_pSourceTextEdit->clear();
    m_pSourceTextEdit->insertPlainText("Source goes here.");

    // Layouts
    QVBoxLayout* pMainLayout = new QVBoxLayout;
    QHBoxLayout* pTopLayout = new QHBoxLayout;
    auto pMainRegion = new QWidget(this);   // whole panel
    auto pTopRegion = new QWidget(this);      // top buttons/edits

    pTopLayout->addWidget(m_pFileSelectCombo);
    pTopLayout->addWidget(m_pInfoLabel);
    pTopLayout->addStretch();
    pMainLayout->addWidget(pTopRegion);
    pMainLayout->addWidget(m_pSourceTextEdit);

    SetMargins(pTopLayout);
    SetMargins(pMainLayout);

    pTopRegion->setLayout(pTopLayout);
    pMainRegion->setLayout(pMainLayout);
    setWidget(pMainRegion);

    loadSettings();

    connect(m_pFileSelectCombo, SIGNAL(currentIndexChanged(int)),       SLOT(fileSelectComboChanged(int)));
    connect(m_pTargetModel,     &TargetModel::connectChangedSignal,     this, &SourceWindow::connectChanged);
    connect(m_pTargetModel,     &TargetModel::startStopChangedSignal,   this, &SourceWindow::startStopChanged);
    connect(m_pTargetModel,     &TargetModel::startStopChangedSignalDelayed,   this, &SourceWindow::startStopDelayed);
    connect(m_pTargetModel,     &TargetModel::mainStateCompletedSignal, this, &SourceWindow::mainStateCompleted);

    connect(m_pSession,         &Session::settingsChanged,              this, &SourceWindow::settingsChanged);
    connect(m_pSession,         &Session::programDatabaseChanged,       this, &SourceWindow::programDatabaseChanged);

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
}

void SourceWindow::saveSettings()
{
    QSettings settings;
    settings.beginGroup("Source");

    settings.setValue("geometry", saveGeometry());
    settings.endGroup();
}

void SourceWindow::fileSelectComboChanged(int index)
{
    if (index < m_pSourceCache->m_fileVec.size())
    {
        SourceCache::File f = m_pSourceCache->m_fileVec[index];
        setFile(f.key);
        setCursorAtLine(0, 0);
    }
}

void SourceWindow::connectChanged()
{
    if (!m_pTargetModel->IsConnected())
        rescanCache();
}

void SourceWindow::startStopChanged()
{
}

void SourceWindow::startStopDelayed(int /*running*/)
{
}

void SourceWindow::mainStateCompleted()
{
    updateCurrentFile();
}

void SourceWindow::programDatabaseChanged()
{
    rescanCache();
}

void SourceWindow::settingsChanged()
{
    const Session::Settings& settings = m_pSession->GetSettings();

    // Update visuals
    const QFont& font = settings.m_font;
    m_pSourceTextEdit->setFont(font);
    QFontMetrics info(font);
    QString padText = QString("W").repeated(settings.m_sourceTabSize);
    m_pSourceTextEdit->setTabStopDistance(info.horizontalAdvance(padText));

    // Source directories might have changed, so reload
    rescanCache();
    updateCurrentFile();
}

void SourceWindow::rescanCache()
{
    // Create a set of directories and files to find.
    QVector<SourceCache::FilePath> files;
    const std::vector<fonda::compilation_unit>& pdb = m_pSession->m_pProgramDatabase->GetFileInfo();

    // Convert from "elf" to a format the SourceCache might like
    for (size_t i = 0; i < pdb.size(); ++i)
    {
        const fonda::compilation_unit& unit = pdb[i];
        for (size_t j = 0; j < unit.files.size(); ++j)
        {
            SourceCache::FilePath fp;
            fp.dir = unit.dirs[unit.files[j].dir_index];
            fp.file = unit.files[j].path;
            files.push_back(fp);
        }
    }

    // ... and a set of search paths
    QVector<QString> searchPaths;

    // Add the directory containing the .elf/.prg as the default
    QString elfPath = m_pSession->m_pProgramDatabase->GetElfPath();
    QFileInfo elf(elfPath);
    searchPaths.push_back(elf.absoluteDir().absolutePath());

    // Add user paths from settings
    for (int i = 0; i < Session::Settings::kNumSearchDirectories; ++i)
    {
        QString userSearchPath = m_pSession->GetSettings().m_sourceSearchDirectories[i];
        if (userSearchPath.size() > 0)
            searchPaths.push_back(userSearchPath);
    }

    m_pSourceCache->Rescan(files, searchPaths);
    m_pSourceFilesModel->emitChanged();
    updateCurrentFile();
}

void SourceWindow::updateCurrentFile()
{
    uint32_t textAddr = m_pTargetModel->GetRegs().Get(Registers::TEXT);
    uint32_t dataAddr = m_pTargetModel->GetRegs().Get(Registers::DATA);
    uint32_t pc = m_pTargetModel->GetStartStopPC(kProcCpu);
    uint32_t textOffset = pc - textAddr;
    ProgramDatabase::CodeInfo info;
    QString txt = "unknown";

    if (pc < dataAddr && m_pSession->m_pProgramDatabase->FindLowerOrEqual(textOffset, info))
    {
        txt = QString::asprintf("PC %x maps to %s/%s line: %d col: %d\n",
                                               pc,
                                               info.m_dir.c_str(),
                                               info.m_file.c_str(),
                                               info.m_line,
                                               info.m_column);
        m_pInfoLabel->setText(txt);
        QString key = SourceCache::GetKey(info.m_dir, info.m_file);

        setFile(key);
        setCursorAtLine(info.m_line, info.m_column);

        // This is different -- use "jump to"?
        m_pSourceTextEdit->setHighlightLine(info.m_line);
        return;
    }
    else
    {
        m_currFileKey.clear();
        m_pSourceTextEdit->setPlainText("No source for this address");
        m_pSourceTextEdit->setHighlightLine(-1);
        m_pInfoLabel->setText(txt);
    }
}

void SourceWindow::setFile(QString key)
{
    if (key != m_currFileKey)
    {
        m_currFileKey = key;
        QString fileText;
        if (!m_pSourceCache->GetFile(key, fileText))
        {
            m_pSourceTextEdit->setPlainText("Can't find source file");
            return;
        }
        m_pSourceTextEdit->setPlainText(fileText);
    }
}

void SourceWindow::setCursorAtLine(int line, int column)
{
    // This is utter garbage, so I need a better approach
    QTextCursor textCursor = m_pSourceTextEdit->textCursor();
    QTextBlock b = textCursor.document()->findBlockByNumber(line - 1);
    textCursor.setPosition(b.position() + column, QTextCursor::MoveAnchor);
    m_pSourceTextEdit->setTextCursor(textCursor);
}
