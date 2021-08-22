#ifndef MAINWINDOW_H
#define MAINWINDOW_H

/*
	Main GUI Window.

	Currently everything is controlled and routed through here.
*/

#include <QMainWindow>
#include "../models/targetmodel.h"
#include "../models/disassembler.h"
#include "../models/session.h"

class QPushButton;
class QLabel;
class QTcpSocket;
class QTextEdit;
class QActionGroup;
class QComboBox;

class Dispatcher;
class TargetModel;

class DisasmWindow;
class MemoryWindow;
class GraphicsInspectorWidget;
class BreakpointsWindow;
class ConsoleWindow;
class ExceptionDialog;
class RunDialog;
class PrefsDialog;

class RegisterWidget : public QWidget
{
    Q_OBJECT
public:
    RegisterWidget(QWidget* parent, Session* pSession);
    virtual ~RegisterWidget() override;

protected:
    virtual void paintEvent(QPaintEvent*) override;
    virtual void mouseMoveEvent(QMouseEvent *event) override;
    virtual void contextMenuEvent(QContextMenuEvent *event) override;
    virtual bool event(QEvent *event) override;

private slots:
    void connectChangedSlot();
    void startStopChangedSlot();
    void registersChangedSlot(uint64_t commandId);
    void memoryChangedSlot(int slot, uint64_t commandId);
    void symbolTableChangedSlot(uint64_t commandId);
    void startStopDelayedSlot(int running);
    void settingsChangedSlot();

    // Callbacks when "show in Memory X" etc is selected
    void disasmViewTrigger(int windowIndex);
    void memoryViewTrigger(int windowIndex);

private:
    void PopulateRegisters();
    void UpdateFont();

    // Tokens etc
    enum TokenType
    {
        kRegister,
        kSymbol,                // aka an arbitray "address"
        kStatusRegisterBit,
        kNone,
    };

    enum TokenColour
    {
        kNormal,
        kChanged,
        kInactive,
        kCode
    };

    struct Token
    {
        int x;
        int y;
        QString text;

        TokenType type;
        uint32_t subIndex;      // subIndex e.g "4" for D4, 0x12345 for symbol address, bitnumber for SR field
        TokenColour colour;     // how to draw it

        QRectF rect;            // bounding rectangle, updated when rendered
    };

    QString FindSymbol(uint32_t addr);

    int AddToken(int x, int y, QString text, TokenType type, uint32_t subIndex = 0, TokenColour colour = TokenColour::kNormal);
    int AddReg16(int x, int y, uint32_t regIndex, const Registers &prevRegs, const Registers &m_currRegs);
    int AddReg32(int x, int y, uint32_t regIndex, const Registers &prevRegs, const Registers &m_currRegs);

    int AddSR(int x, int y, const Registers &prevRegs, const Registers &m_currRegs, uint32_t bit, const char *pName);
    int AddSymbol(int x, int y, uint32_t address);

    QString GetTooltipText(const Token& token);
    void UpdateTokenUnderMouse();

    // Convert from row ID to a pixel Y (top pixel in the drawn row)
    int GetPixelFromRow(int row) const;

    // Convert from pixel Y to a row ID
    int GetRowFromPixel(int y) const;

    // UI Elements
    QAction*                    m_pShowDisasmWindowActions[kNumDisasmViews];
    QAction*                    m_pShowMemoryWindowActions[kNumMemoryViews];

    Session*                    m_pSession;
    Dispatcher*             	m_pDispatcher;
    TargetModel*                m_pTargetModel;

    // Shown data
    Registers                   m_currRegs;     // current regs
    Registers                   m_prevRegs;     // regs when PC started
    Disassembler::disassembly   m_disasm;

    QVector<Token>              m_tokens;

    // Mouse data
    QPointF                     m_mousePos;                  // last updated position
    int                         m_tokenUnderMouseIndex;      // -1 for none
    Token                       m_tokenUnderMouse;           // copy of relevant token (for menus etc)
    uint32_t                    m_addressUnderMouse;

    // Render info
    QFont                       m_monoFont;
    int                         m_yAscent;        // Font ascent (offset from top for drawing)
    int                         m_lineHeight;
    int                         m_charWidth;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    virtual void closeEvent(QCloseEvent *event);

private slots:
    void connectChangedSlot();
	void startStopChangedSlot();
    void memoryChangedSlot(int slot, uint64_t commandId);

    // Button callbacks
    void startStopClicked();
    void singleStepClicked();
    void nextClicked();
    void runToClicked();
    void addBreakpointPressed();
    void breakPressed();

    // Menu item callbacks
    void menuConnect();
    void menuDisconnect();

    void about();
    void aboutQt();
private:
    void updateWindowMenu();

    // QAction callbacks
    // File Menu
    void RunTriggered();
    void ConnectTriggered();
    void DisconnectTriggered();

    // Exception Menu
    void ExceptionsDialogTriggered();
    void PrefsDialogTriggered();

	// Populaters
    void PopulateRunningSquare();
    void updateButtonEnable();

    // Settings
    void loadSettings();
    void saveSettings();

    // Our UI widgets
    QWidget*        m_pRunningSquare;
    QPushButton*	m_pStartStopButton;
    QPushButton*	m_pStepIntoButton;
    QPushButton*	m_pStepOverButton;
    QPushButton*	m_pRunToButton;
    QComboBox*      m_pRunToCombo;

    RegisterWidget* m_pRegisterWidget;

    // Dialogs
    ExceptionDialog*    m_pExceptionDialog;
    RunDialog*          m_pRunDialog;
    PrefsDialog*        m_pPrefsDialog;

    // Docking windows
    DisasmWindow*               m_pDisasmWidgets[kNumDisasmViews];
    MemoryWindow*               m_pMemoryViewWidgets[kNumMemoryViews];
    GraphicsInspectorWidget*    m_pGraphicsInspector;
    BreakpointsWindow*          m_pBreakpointsWidget;
    ConsoleWindow*              m_pConsoleWindow;

    // Low-level data
    Session                     m_session;
    Dispatcher*             	m_pDispatcher;
    TargetModel*                m_pTargetModel;

    // Target data -- used for single-stepping
    Disassembler::disassembly   m_disasm;

    // Menus
    void createActions();
    void createMenus();

    // Shared function to show a sub-window, called by Action callbacks
    void enableVis(QWidget *pWidget);

    QMenu* m_pFileMenu;
    QMenu* m_pEditMenu;
    QMenu* m_pWindowMenu;
    QMenu* m_pHelpMenu;

    QAction* m_pRunAct;
    QAction* m_pConnectAct;
    QAction* m_pDisconnectAct;
    QAction* m_pExitAct;

    QAction* m_pExceptionsAct;
    QAction* m_pPrefsAct;

    // Multiple actions, one for each window
    QAction* m_pDisasmWindowActs[kNumDisasmViews];
    // Multiple actions, one for each window
    QAction* m_pMemoryWindowActs[kNumMemoryViews];
    QAction* m_pGraphicsInspectorAct;
    QAction* m_pBreakpointsWindowAct;
    QAction* m_pConsoleWindowAct;

    QAction* m_pAboutAct;
    QAction* m_pAboutQtAct;
};
#endif // MAINWINDOW_H
