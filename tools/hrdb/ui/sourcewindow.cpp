#include "sourcewindow.h"

#include <iostream>
#include <QDebug>
#include <QTextEdit>
#include <QSettings>
#include <QVBoxLayout>
#include <QLabel>

#include <QFile>
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
    pTopLayout->addStretch();

    pMainLayout->addWidget(pTopRegion);
    pMainLayout->addWidget(m_pInfoLabel);
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
        txt = QString::asprintf("PC %x maps to %s/%s line: %d col: %d\n",
                                               pc,
                                               info.m_dir.c_str(),
                                               info.m_file.c_str(),
                                               info.m_line,
                                               info.m_column);
        m_pInfoLabel->setText(txt);

        const SourceFile* pFileData;
        QString key = SourceCache::GetKey(info.m_dir, info.m_file);
        if (key != m_currFileKey)
        {
            if (!m_pSourceCache->GetFile(info.m_dir, info.m_file, pFileData))
            {
                m_pSourceTextEdit->setPlainText("Can't find source file");
                return;
            }

            m_pSourceTextEdit->setPlainText(pFileData->text);
            m_currFileKey = key;
        }
        // This is utter garbage, so I need a better approach
        QTextCursor textCursor = m_pSourceTextEdit->textCursor();
        QTextBlock b = textCursor.document()->findBlockByNumber(info.m_line - 1);
        textCursor.setPosition(b.position() + info.m_column, QTextCursor::MoveAnchor);
        m_pSourceTextEdit->setTextCursor(textCursor);
        m_pSourceTextEdit->setCurrentLine(info.m_line);
        //m_pSourceTextEdit->moveCursor(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        return;
    }
    else
    {
        m_currFileKey = "";
        m_pSourceTextEdit->setPlainText("No source for this address");
        m_pInfoLabel->setText(txt);
    }
}

void SourceWindow::settingsChanged()
{
    // Update visuals
    const QFont& font = m_pSession->GetSettings().m_font;
    m_pSourceTextEdit->setFont(font);
    QFontMetrics info(font);
    m_pSourceTextEdit->setTabStopDistance(info.horizontalAdvance("WWWWWWWWWW", 4));
}

