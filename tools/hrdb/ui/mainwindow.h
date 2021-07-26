#ifndef MAINWINDOW_H
#define MAINWINDOW_H

/*
	Main GUI Window.

	Currently everything is controlled and routed through here.
*/

#include <QMainWindow>
#include "../models/targetmodel.h"
#include "../models/disassembler.h"

class QPushButton;
class QLabel;
class QTcpSocket;
class QTextEdit;
class QActionGroup;
class QComboBox;

class Dispatcher;
class TargetModel;

class DisasmViewWidget;
class MemoryViewWidget;
class GraphicsInspectorWidget;
class BreakpointsWidget;
class ExceptionDialog;
class RunDialog;

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
    void registersChangedSlot(uint64_t commandId);
    void memoryChangedSlot(int slot, uint64_t commandId);
    void symbolTableChangedSlot(uint64_t commandId);
    void startStopDelayedSlot(int running);

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
    // File Menu
    void Run();
    void Connect();
    void Disconnect();

    // Exception Menu
    void ExceptionsDialog();

	// Populaters
	void PopulateRegisters();
    QString FindSymbol(uint32_t addr);
    void PopulateRunningSquare();
    void updateButtonEnable();

    // Settings
    void loadSettings();
    void saveSettings();

    // Our UI widgets
    QPushButton*	m_pStartStopButton;
    QPushButton*	m_pStepIntoButton;
    QPushButton*	m_pStepOverButton;
    QPushButton*	m_pRunToButton;
    QComboBox*      m_pRunToCombo;
	QTextEdit*		m_pRegistersTextEdit;
    QWidget*        m_pRunningSquare;

    // Dialogs
    ExceptionDialog*    m_pExceptionDialog;
    RunDialog*          m_pRunDialog;

    // Docking windows
    DisasmViewWidget*               m_pDisasmWidget0;
    DisasmViewWidget*               m_pDisasmWidget1;
    MemoryViewWidget*           m_pMemoryViewWidget0;
    MemoryViewWidget*           m_pMemoryViewWidget1;
    GraphicsInspectorWidget*    m_pGraphicsInspector;
    BreakpointsWidget*          m_pBreakpointsWidget;

    // Low-level data
	QTcpSocket* 	tcpSocket;
    Dispatcher*		m_pDispatcher;
	TargetModel*	m_pTargetModel;

    // Shown data
    Registers                      m_prevRegs;
    Disassembler::disassembly      m_disasm;

    // Menus
    void createActions();
    void createMenus();
    void enableVis(QWidget *pWidget);
    QMenu *fileMenu;
    QMenu *editMenu;
    QMenu *windowMenu;
    QMenu *helpMenu;

    QAction *runAct;
    QAction *connectAct;
    QAction *disconnectAct;
    QAction *exitAct;

    QAction *exceptionsAct;

    QAction *disasmWindowAct0;
    QAction *disasmWindowAct1;
    QAction *memoryWindowAct0;
    QAction *memoryWindowAct1;
    QAction *graphicsInspectorAct;
    QAction *breakpointsWindowAct;

    QAction *aboutAct;
    QAction *aboutQtAct;
};
#endif // MAINWINDOW_H
