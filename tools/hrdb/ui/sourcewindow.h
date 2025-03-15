#ifndef SOURCEWINDOW_H
#define SOURCEWINDOW_H

#include <QDockWidget>
#include "showaddressactions.h"

class TargetModel;
class Dispatcher;
class Session;
class QLabel;
class QTextEdit;
class QComboBox;

class SourceCache;
class SourceFilesModel;

#include <QPlainTextEdit>

QT_BEGIN_NAMESPACE
class QPaintEvent;
class QResizeEvent;
class QSize;
class QWidget;
class QLabel;
QT_END_NAMESPACE

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

private:
    void connectChanged();
    void startStopChanged();
    void startStopDelayed(int running);
    void mainStateCompleted();

    void programDatabaseChanged();
    void settingsChanged();

    void rescanCache();
    void updateCurrentFile();

    void setFile(QString key);
    void setCursorAtLine(int line, int column);

    Session*            m_pSession;
    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;

    CodeEditor*         m_pSourceTextEdit;
    QComboBox*          m_pFileSelectCombo;
    QLabel*             m_pInfoLabel;

    // Cache of files
    SourceCache*        m_pSourceCache;
    SourceFilesModel*   m_pSourceFilesModel;
    QString             m_currFileKey;
};

#endif // SOURCEWINDOW_H
