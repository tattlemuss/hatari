#ifndef SOURCEWINDOW_H
#define SOURCEWINDOW_H

#include <QDockWidget>
#include <QPlainTextEdit>
#include "showaddressactions.h"
#include "models/programdatabase.h"
#include "models/session.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QTextEdit;
class QComboBox;
class QCheckBox;
class QPaintEvent;
class QResizeEvent;
class QSize;
class QWidget;
QT_END_NAMESPACE


class TargetModel;
class Dispatcher;

class SourceCache;
class SourceFilesModel;
class CodeEditor;

class LineNumberArea;

//![codeeditordefinition]

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    CodeEditor(QWidget *parent = nullptr);

    void lineNumberAreaPaintEvent(QPaintEvent *event);
    int lineNumberAreaWidth();

    void setHighlightLine(int line)
    {
        m_currentLine = line;
        highlightCurrentLine();
    }

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect &rect, int dy);

private:
    QWidget *lineNumberArea;
    int m_currentLine;
};

//![codeeditordefinition]
//![extraarea]

class LineNumberArea : public QWidget
{
public:
    LineNumberArea(CodeEditor *editor) : QWidget(editor), codeEditor(editor)
    {}

    QSize sizeHint() const override
    {
        return QSize(codeEditor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        codeEditor->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor *codeEditor;
};

//![extraarea]



class SourceWindow : public QDockWidget
{
    Q_OBJECT
public:
    SourceWindow(QWidget *parent, Session* pSession);
    virtual ~SourceWindow() override;

    // Grab focus and point to the main widget
    void keyFocus();

    void loadSettings();
    void saveSettings();

private slots:
    void fileSelectComboChanged(int index);
private:
    void connectChanged();
    void mainStateCompleted();
    void followPCChanged();

    void programDatabaseChanged();
    void settingsChanged();
    void addressRequested(Session::WindowType type, int windowIndex, int /*memorySpace*/, uint32_t address);

    void rescanCache();
    void updateFollowedPC();

    bool showFileForAddress(uint32_t addr, bool jumpCursor);
    void setFile(QString key);
    void setCursorAtLine(int line, int column);
    bool GetLineInfo(ProgramDatabase::CodeInfo& info, uint32_t addr);

    Session*            m_pSession;
    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;

    CodeEditor*         m_pSourceTextEdit;
    QComboBox*          m_pFileSelectCombo;
    QCheckBox*          m_pFollowPCCheckBox;

    // Cache of files
    SourceCache*        m_pSourceCache;
    SourceFilesModel*   m_pSourceFilesModel;
    QString             m_currFileKey;

    bool                m_bFollowPC;
};

#endif // SOURCEWINDOW_H
