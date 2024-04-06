#ifndef MAINWINDOW_H
#define MAINWINDOW_H

/*
    Main GUI Window.

    Currently everything is controlled and routed through here.
*/

#include <QMainWindow>
#include "../models/memory.h"
#include "../models/disassembler.h"
#include "../models/targetmodel.h"

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
class HardwareWindow;
class ProfileWindow;
class RegisterWidget;
class ExceptionDialog;
class RunDialog;
class PrefsDialog;
class Session;

///
/// \brief This is the main application window and contains all the sub-widgets,
/// hence the massive number of class inclusions
///
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Session& session, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    virtual void closeEvent(QCloseEvent *event) override;

private:
    enum RunToMode
    {
        kRunToRts,
        kRunToRte,
        kRunToVbl,
        kRunToHbl,
        kRunToRam,
        kRunToMax,
    };

private slots:
    void startStopClickedSlot();
    void singleStepClickedSlot();
    void nextClickedSlot();
    void skipPressedSlot();
    void runToClickedSlot();
    void cycleRunToSlot();
    void breakPressedSlot();

private:
    void connectChanged();
    void startStopChanged();
    void memoryChanged(int slot, uint64_t commandId);
    void runningRefreshTimer();
    void flush(const TargetChangedFlags& flags, uint64_t commandId);
    void protocolMismatch(uint32_t hatariProtocol, uint32_t hrdbProtocol);
    void saveBinComplete(uint64_t commandId);

    // Button callbacks
    void addBreakpointPressed();

    // Menu item callbacks
    void menuConnect();
    void menuDisconnect();

    void about();
    void aboutQt();
    void onlineHelp();

    // status
    void messageSet(const QString& msg);
    void requestMainState(uint32_t pc);
    void updateWindowMenu();

    // QAction callbacks
    // File Menu
    void LaunchTriggered();
    void QuickLaunchTriggered();
    void ConnectTriggered();
    void DisconnectTriggered();
    void WarmResetTriggered();
    void FastForwardTriggered();

    // Exception Menu
    void ExceptionsDialogTriggered();
    void PrefsDialogTriggered();

    // Populaters
    void PopulateRunningSquare();
    void updateButtonEnable();

    // Settings
    void loadSettings();
    void saveSettings();

    // Exection
    void runTo(RunToMode mode);

private slots:
    void runToRtsSlot() { runTo(RunToMode::kRunToRts); }
    void runToRteSlot() { runTo(RunToMode::kRunToRte); }
    void runToVblSlot() { runTo(RunToMode::kRunToVbl); }
    void runToHblSlot() { runTo(RunToMode::kRunToHbl); }
    void runToRamSlot() { runTo(RunToMode::kRunToRam); }

private:
    // Our UI widgets
    QWidget*        m_pRunningSquare;
    QPushButton*    m_pStartStopButton;
    QPushButton*    m_pStepIntoButton;
    QPushButton*    m_pStepOverButton;
    QPushButton*    m_pRunToButton;
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
    HardwareWindow*             m_pHardwareWindow;
    ProfileWindow*              m_pProfileWindow;

    // Low-level data
    Session&                    m_session;
    Dispatcher*                 m_pDispatcher;
    TargetModel*                m_pTargetModel;

    // Target data -- used for single-stepping
    Disassembler::disassembly   m_disasm;

    // Flush request made after all main state is fetched
    uint64_t                    m_mainStateUpdateRequest;

    // Flush request made by live update (fetching registers)
    uint64_t                    m_liveRegisterReadRequest;

    // Menus
    void createActions();
    void createToolBar();
    void createMenus();

    // Shared function to show a sub-window, called by Action callbacks
    void enableVis(QDockWidget *pWidget);

    QMenu* m_pFileMenu;
    QMenu* m_pEditMenu;
    QMenu* m_pWindowMenu;
    QMenu* m_pHelpMenu;

    QAction* m_pLaunchAct;
    QAction* m_pQuickLaunchAct;
    QAction* m_pConnectAct;
    QAction* m_pDisconnectAct;
    QAction* m_pWarmResetAct;
    QAction* m_pFastForwardAct;

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
    QAction* m_pHardwareWindowAct;
    QAction* m_pProfileWindowAct;

    QAction* m_pOnlineHelpAct;
    QAction* m_pAboutAct;
    QAction* m_pAboutQtAct;
};
#endif // MAINWINDOW_H
