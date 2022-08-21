#include "mainwindow.h"

#include <iostream>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QToolBar>

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../hardware/regs_st.h"

#include "disasmwidget.h"
#include "memoryviewwidget.h"
#include "graphicsinspector.h"
#include "breakpointswidget.h"
#include "registerwidget.h"
#include "consolewindow.h"
#include "hardwarewindow.h"
#include "profilewindow.h"
#include "addbreakpointdialog.h"
#include "exceptiondialog.h"
#include "rundialog.h"
#include "quicklayout.h"
#include "prefsdialog.h"

MainWindow::MainWindow(Session& session, QWidget *parent)
    : QMainWindow(parent),
      m_session(session),
      m_mainStateUpdateRequest(0),
      m_liveRegisterReadRequest(0)
{
    setObjectName("MainWindow");
    m_pTargetModel = m_session.m_pTargetModel;
    m_pDispatcher = m_session.m_pDispatcher;

    // Creation - done in Tab order
    // Register/status window
    m_pRegisterWidget = new RegisterWidget(this, &m_session);

    // Top row of buttons
    m_pRunningSquare = new QWidget(this);
    m_pRunningSquare->setFixedSize(10, 25);
    m_pRunningSquare->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));
    m_pStartStopButton = new QPushButton("Break", this);
    m_pStepIntoButton = new QPushButton("(S)tep", this);
    m_pStepOverButton = new QPushButton("(N)ext", this);

    m_pStartStopButton->setToolTip("Ctrl+R: Run/Stop, Esc: Stop");
    m_pStepIntoButton->setToolTip("S: Execute one instruction.\n"
            "Jumps into subroutines.\n"
            "Shift+S: skip instruction.");
    m_pStepOverButton->setToolTip("N: Stop at next instruction in memory.\n"
            "Jumps over subroutines and through backwards loops.");

    m_pRunToButton = new QPushButton("Run (U)ntil:", this);
    m_pRunToButton->setToolTip("U: Run until specified condition");

    m_pRunToCombo = new QComboBox(this);
    m_pRunToCombo->insertItem(0, "RTS");
    m_pRunToCombo->insertItem(1, "RTE");
    m_pRunToCombo->insertItem(2, "Next VBL");
    m_pRunToCombo->insertItem(3, "Next HBL");

    for (int i = 0; i < kNumDisasmViews; ++i)
    {
        m_pDisasmWidgets[i] = new DisasmWindow(this, &m_session, i);
        if (i == 0)
            m_pDisasmWidgets[i]->setWindowTitle("Disassembly 1 (Alt+D)");
        else
            m_pDisasmWidgets[i]->setWindowTitle(QString::asprintf("Disassembly %d", i + 1));
    }

    for (int i = 0; i < kNumMemoryViews; ++i)
    {
        m_pMemoryViewWidgets[i] = new MemoryWindow(this, &m_session, i);
        if (i == 0)
            m_pMemoryViewWidgets[i]->setWindowTitle("Memory 1 (Alt+M)");
        else
            m_pMemoryViewWidgets[i]->setWindowTitle(QString::asprintf("Memory %d", i + 1));
    }

    m_pGraphicsInspector = new GraphicsInspectorWidget(this, &m_session);
    m_pGraphicsInspector->setWindowTitle("Graphics Inspector (Alt+G)");
    m_pBreakpointsWidget = new BreakpointsWindow(this, &m_session);
    m_pBreakpointsWidget->setWindowTitle("Breakpoints (Alt+B)");
    m_pConsoleWindow = new ConsoleWindow(this, &m_session);

    m_pHardwareWindow = new HardwareWindow(this, &m_session);
    m_pHardwareWindow->setWindowTitle("Hardware (Alt+H)");
    m_pProfileWindow = new ProfileWindow(this, &m_session);

    m_pExceptionDialog = new ExceptionDialog(this, m_pTargetModel, m_pDispatcher);
    m_pRunDialog = new RunDialog(this, &m_session);
    m_pPrefsDialog = new ::PrefsDialog(this, &m_session);

    // https://doc.qt.io/qt-5/qtwidgets-layouts-basiclayouts-example.html
    QVBoxLayout *vlayout = new QVBoxLayout;
    QHBoxLayout *hlayout = new QHBoxLayout;
    auto pTopGroupBox = new QWidget(this);
    auto pMainGroupBox = new QGroupBox(this);

    SetMargins(hlayout);
    hlayout->setAlignment(Qt::AlignLeft);
    hlayout->addWidget(m_pRunningSquare);
    hlayout->addWidget(m_pStartStopButton);
    hlayout->addWidget(m_pStepIntoButton);
    hlayout->addWidget(m_pStepOverButton);
    hlayout->addWidget(m_pRunToButton);
    hlayout->addWidget(m_pRunToCombo);
    hlayout->addStretch();
    //hlayout->setAlignment(m_pRunToCombo, Qt::Align);
    pTopGroupBox->setLayout(hlayout);

    SetMargins(vlayout);
    vlayout->addWidget(pTopGroupBox);
    vlayout->addWidget(m_pRegisterWidget);
    vlayout->setAlignment(Qt::Alignment(Qt::AlignTop));
    pMainGroupBox->setFlat(true);
    pMainGroupBox->setLayout(vlayout);

    setCentralWidget(pMainGroupBox);

    for (int i = 0; i < kNumDisasmViews; ++i)
        this->addDockWidget(Qt::BottomDockWidgetArea, m_pDisasmWidgets[i]);
    for (int i = 0; i < kNumMemoryViews; ++i)
        this->addDockWidget(Qt::RightDockWidgetArea, m_pMemoryViewWidgets[i]);

    this->addDockWidget(Qt::LeftDockWidgetArea, m_pGraphicsInspector);
    this->addDockWidget(Qt::BottomDockWidgetArea, m_pBreakpointsWidget);
    this->addDockWidget(Qt::BottomDockWidgetArea, m_pConsoleWindow);
    this->addDockWidget(Qt::RightDockWidgetArea, m_pHardwareWindow);
    this->addDockWidget(Qt::RightDockWidgetArea, m_pProfileWindow);

    loadSettings();

    // Set up menus (reflecting current state)
    createActions();
    createMenus();
    createToolBar();

    // Listen for target changes
    connect(m_pTargetModel, &TargetModel::startStopChangedSignal,    this, &MainWindow::startStopChangedSlot);
    connect(m_pTargetModel, &TargetModel::connectChangedSignal,      this, &MainWindow::connectChangedSlot);
    connect(m_pTargetModel, &TargetModel::memoryChangedSignal,       this, &MainWindow::memoryChangedSlot);
    connect(m_pTargetModel, &TargetModel::runningRefreshTimerSignal, this, &MainWindow::runningRefreshTimerSlot);
    connect(m_pTargetModel, &TargetModel::flushSignal,               this, &MainWindow::flushSlot);

    // Wire up buttons to actions
    connect(m_pStartStopButton, &QAbstractButton::clicked, this, &MainWindow::startStopClicked);
    connect(m_pStepIntoButton, &QAbstractButton::clicked, this, &MainWindow::singleStepClicked);
    connect(m_pStepOverButton, &QAbstractButton::clicked, this, &MainWindow::nextClicked);
    connect(m_pRunToButton, &QAbstractButton::clicked, this, &MainWindow::runToClicked);

    // Wire up menu appearance
    connect(m_pWindowMenu, &QMenu::aboutToShow, this, &MainWindow::updateWindowMenu);

    // Keyboard shortcuts
    new QShortcut(QKeySequence("Ctrl+R"),         this, SLOT(startStopClicked()));
    new QShortcut(QKeySequence("Esc"),            this, SLOT(breakPressed()));
    new QShortcut(QKeySequence("S"),              this, SLOT(singleStepClicked()));
    new QShortcut(QKeySequence("Ctrl+S"),         this, SLOT(skipPressed()));
    new QShortcut(QKeySequence("N"),              this, SLOT(nextClicked()));
    new QShortcut(QKeySequence("U"),              this, SLOT(runToClicked()));

    // Try initial connect
    ConnectTriggered();

    // Update everything
    connectChangedSlot();
    startStopChangedSlot();

//    m_pDisasmWidget0->keyFocus();
}

MainWindow::~MainWindow()
{
    delete m_pDispatcher;
    delete m_pTargetModel;
}

void MainWindow::connectChangedSlot()
{
    PopulateRunningSquare();
    updateButtonEnable();

    //if (m_pTargetModel->IsConnected())
    //    m_pDispatcher->SendCommandPacket("profile 1");
}

void MainWindow::startStopChangedSlot()
{
    bool isRunning = m_pTargetModel->IsRunning();

    // Update text here
    if (!isRunning)
    {
        // STOPPED
        // TODO this is where all windows should put in requests for data
        // The Main Window does this and other windows feed from it.
        // NOTE: we assume here that PC is already updated (normally this
        // is done with a notification at the stop)
        requestMainState(m_pTargetModel->GetStartStopPC());
    }
    PopulateRunningSquare();
    updateButtonEnable();
}

void MainWindow::memoryChangedSlot(int slot, uint64_t /*commandId*/)
{
    if (slot != MemorySlot::kMainPC)
        return;

    // This is the last part of the main state update, so flag it
    emit m_session.mainStateUpdated();

    // Disassemble the first instruction
    m_disasm.lines.clear();
    const Memory* pMem = m_pTargetModel->GetMemory(MemorySlot::kMainPC);
    if (!pMem)
        return;

    // Fetch data and decode the next instruction.
    buffer_reader disasmBuf(pMem->GetData(), pMem->GetSize(), pMem->GetAddress());
    Disassembler::decode_buf(disasmBuf, m_disasm, pMem->GetAddress(), 1);
}

void MainWindow::runningRefreshTimerSlot()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_session.GetSettings().m_liveRefresh)
    {
        // This will trigger an update of the RegisterWindow
        m_pDispatcher->ReadRegisters();
        m_liveRegisterReadRequest = m_pDispatcher->InsertFlush();
    }
}

void MainWindow::flushSlot(const TargetChangedFlags& /*flags*/, uint64_t commandId)
{
    if (commandId == m_liveRegisterReadRequest)
    {
        // Now that we have registers from the live request, get disassmbly memory
        // using the full register bank
        requestMainState(m_pTargetModel->GetRegs().Get(Registers::PC));
        m_liveRegisterReadRequest = 0;
    }
    else if (commandId == m_mainStateUpdateRequest)
    {
        // This is where we should
        emit m_session.mainStateUpdated();
        m_mainStateUpdateRequest = 0;
    }
}

void MainWindow::startStopClicked()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_pTargetModel->IsRunning())
        m_pDispatcher->Break();
    else
        m_pDispatcher->Run();
}

void MainWindow::singleStepClicked()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_pTargetModel->IsRunning())
        return;

    m_pDispatcher->Step();
}

void MainWindow::nextClicked()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_pTargetModel->IsRunning())
        return;

    // Work out where the next PC is
    if (m_disasm.lines.size() == 0)
        return;

    // Bug fix: we can't decide on how to step until the available disassembly matches
    // the PC we are stepping from. This slows down stepping a little (since there is
    // a round-trip). In theory we could send the next instruction opcode as part of
    // the "status" notification if we want it to be faster.
    if(m_disasm.lines[0].address != m_pTargetModel->GetStartStopPC())
        return;

    const Disassembler::line& nextInst = m_disasm.lines[0];
    // Either "next" or set breakpoint to following instruction
    bool shouldStepOver = DisAnalyse::isSubroutine(nextInst.inst) ||
                          DisAnalyse::isTrap(nextInst.inst) ||
                          DisAnalyse::isBackDbf(nextInst.inst);
    if (shouldStepOver)
    {
        uint32_t next_pc = nextInst.inst.byte_count + nextInst.address;
        m_pDispatcher->RunToPC(next_pc);
    }
    else
    {
        m_pDispatcher->Step();
    }
}

void MainWindow::skipPressed()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_pTargetModel->IsRunning())
        return;

    // Work out where the next PC is
    if (m_disasm.lines.size() == 0)
        return;

    // Bug fix: we can't decide on how to step until the available disassembly matches
    // the PC we are stepping from. This slows down stepping a little (since there is
    // a round-trip). In theory we could send the next instruction opcode as part of
    // the "status" notification if we want it to be faster.
    if(m_disasm.lines[0].address != m_pTargetModel->GetStartStopPC())
        return;

    const Disassembler::line& nextInst = m_disasm.lines[0];
    m_pDispatcher->SetRegister(Registers::PC, nextInst.GetEnd());
}

void MainWindow::runToClicked()
{
    if (!m_pTargetModel->IsConnected())
        return;
    if (m_pTargetModel->IsRunning())
        return;

    if (m_pRunToCombo->currentIndex() == 0)
        m_pDispatcher->SetBreakpoint("(pc).w = $4e75", true);      // RTS
    else if (m_pRunToCombo->currentIndex() == 1)
        m_pDispatcher->SetBreakpoint("(pc).w = $4e73", true);      // RTE
    else if (m_pRunToCombo->currentIndex() == 2)
        m_pDispatcher->SetBreakpoint("VBL ! VBL", true);        // VBL
        //m_pDispatcher->SetBreakpoint("pc = ($70).l", true);        // VBL interrupt code
    else if (m_pRunToCombo->currentIndex() == 3)
        m_pDispatcher->SetBreakpoint("HBL ! HBL", true);        // VBL
    else
        return;
    m_pDispatcher->Run();
}

void MainWindow::addBreakpointPressed()
{
    AddBreakpointDialog dialog(this, m_pTargetModel, m_pDispatcher);
    dialog.exec();
}

void MainWindow::breakPressed()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_pTargetModel->IsRunning())
        m_pDispatcher->Break();
}

// Actions
void MainWindow::LaunchTriggered()
{
    m_pRunDialog->setModal(true);
    m_pRunDialog->show();
    // We can't connect here since the dialog hasn't really run yet.
}

void MainWindow::QuickLaunchTriggered()
{
    LaunchHatari(m_session.GetLaunchSettings(), &m_session);
}

void MainWindow::ConnectTriggered()
{
    m_session.Connect();
}

void MainWindow::DisconnectTriggered()
{
    m_session.Disconnect();
}

void MainWindow::WarmResetTriggered()
{
    m_pDispatcher->ResetWarm();
    // This will re-request from Hatari, which should return
    // an empty symbol table.
    m_pDispatcher->ReadSymbols();

    // Restart if in break mode
    if (!m_pTargetModel->IsRunning())
        m_pDispatcher->Run();
}

void MainWindow::FastForwardTriggered()
{
    m_pDispatcher->SetFastForward(m_pFastForwardAct->isChecked());
}

void MainWindow::ExceptionsDialogTriggered()
{
    m_pExceptionDialog->setModal(true);
    m_pExceptionDialog->show();
}

void MainWindow::PrefsDialogTriggered()
{
    m_pPrefsDialog->setModal(true);
    m_pPrefsDialog->show();
}

void MainWindow::PopulateRunningSquare()
{
    QPalette pal = m_pRunningSquare->palette();

    // set black background
    QColor col = Qt::red;
    if (!m_pTargetModel->IsConnected())
    {
        col = Qt::gray;
    }
    else if (m_pTargetModel->IsRunning())
    {
        col = Qt::green;
    }
    pal.setColor(QPalette::Background, col);
    m_pRunningSquare->setAutoFillBackground(true);
    m_pRunningSquare->setPalette(pal);
}

void MainWindow::updateWindowMenu()
{
    for (int i = 0; i < kNumDisasmViews; ++i)
        m_pDisasmWindowActs[i]->setChecked(m_pDisasmWidgets[i]->isVisible());

    for (int i = 0; i < kNumMemoryViews; ++i)
        m_pMemoryWindowActs[i]->setChecked(m_pMemoryViewWidgets[i]->isVisible());

    m_pGraphicsInspectorAct->setChecked(m_pGraphicsInspector->isVisible());
    m_pBreakpointsWindowAct->setChecked(m_pBreakpointsWidget->isVisible());
    m_pConsoleWindowAct->setChecked(m_pConsoleWindow->isVisible());
    m_pHardwareWindowAct->setChecked(m_pHardwareWindow->isVisible());
    m_pProfileWindowAct->setChecked(m_pProfileWindow->isVisible());
}

void MainWindow::updateButtonEnable()
{
    bool isConnected = m_pTargetModel->IsConnected();
    bool isRunning = m_pTargetModel->IsRunning();

    // Buttons...
    m_pStartStopButton->setEnabled(isConnected);
    m_pStartStopButton->setText(isRunning ? "Break" : "Run");
    m_pStepIntoButton->setEnabled(isConnected && !isRunning);
    m_pStepOverButton->setEnabled(isConnected && !isRunning);
    m_pRunToButton->setEnabled(isConnected && !isRunning);

    // Menu items...
    m_pConnectAct->setEnabled(!isConnected);
    m_pDisconnectAct->setEnabled(isConnected);
    m_pWarmResetAct->setEnabled(isConnected);
    m_pExceptionsAct->setEnabled(isConnected);
    m_pFastForwardAct->setEnabled(isConnected);
    m_pFastForwardAct->setChecked(m_pTargetModel->IsFastForward());
}

void MainWindow::loadSettings()
{
    //https://doc.qt.io/qt-5/qsettings.html#details
    QSettings settings;

    settings.beginGroup("MainWindow");
    restoreGeometry(settings.value("geometry").toByteArray());
    if(!restoreState(settings.value("windowState").toByteArray()))
    {
        // Default docking status
        for (int i = 0; i < kNumDisasmViews; ++i)
            m_pDisasmWidgets[i]->setVisible(i == 0);
        for (int i = 0; i < kNumMemoryViews; ++i)
            m_pMemoryViewWidgets[i]->setVisible(i == 0);
        m_pGraphicsInspector->setVisible(true);
        m_pBreakpointsWidget->setVisible(true);
        m_pConsoleWindow->setVisible(false);
        m_pHardwareWindow->setVisible(false);
        m_pProfileWindow->setVisible(false);
    }
    else
    {
        QDockWidget* wlist[] =
        {
            m_pBreakpointsWidget, m_pGraphicsInspector,
            m_pConsoleWindow, m_pHardwareWindow,
            m_pProfileWindow,
            nullptr
        };
        QDockWidget** pCurr = wlist;
        while (*pCurr)
        {
            // Fix for docking system: for some reason, we need to manually
            // activate floating docking windows for them to appear
            if ((*pCurr)->isFloating())
            {
                (*pCurr)->activateWindow();
            }
            ++pCurr;
        }
        for (int i = 0; i < kNumDisasmViews; ++i)
            if (m_pDisasmWidgets[i]->isFloating())
                m_pDisasmWidgets[i]->activateWindow();
        for (int i = 0; i < kNumMemoryViews; ++i)
            if (m_pMemoryViewWidgets[i]->isFloating())
                m_pMemoryViewWidgets[i]->activateWindow();
    }

    m_pRunToCombo->setCurrentIndex(settings.value("runto", QVariant(0)).toInt());
    settings.endGroup();
}

void MainWindow::saveSettings()
{
    // enclose in scope so it's saved before widgets are saved
    {
        QSettings settings;
        settings.beginGroup("MainWindow");
        settings.setValue("geometry", saveGeometry());
        settings.setValue("windowState", saveState());
        settings.setValue("runto", m_pRunToCombo->currentIndex());
        settings.endGroup();
    }
    for (int i = 0; i < kNumDisasmViews; ++i)
        m_pDisasmWidgets[i]->saveSettings();
    for (int i = 0; i < kNumMemoryViews; ++i)
        m_pMemoryViewWidgets[i]->saveSettings();
    m_pGraphicsInspector->saveSettings();
    m_pConsoleWindow->saveSettings();
    m_pHardwareWindow->saveSettings();
    m_pProfileWindow->saveSettings();
}

void MainWindow::menuConnect()
{
    ConnectTriggered();
}

void MainWindow::menuDisconnect()
{
    DisconnectTriggered();
}

void MainWindow::about()
{
    QMessageBox box;

    QString text = "<h1>hrdb - Hatari remote debugger GUI</h1>\n"
                   "<p>Released under a GPL licence.</p>"
                  "<p><a href=\"https://github.com/tattlemuss/hatari\">Github Repository</a></p>\n"
                   "<p>Version: " VERSION_STRING "</p>";

    QString gplText =
"This program is free software; you can redistribute it and/or modify"
"<br/>it under the terms of the GNU General Public License as published by"
"<br/>the Free Software Foundation; either version 2 of the License, or"
"<br/>(at your option) any later version."
"<br/>"
"<br/>This program is distributed in the hope that it will be useful,"
"<br/>but WITHOUT ANY WARRANTY; without even the implied warranty of"
"<br/>MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the"
"<br/>GNU General Public License for more details.";

    text += gplText;

    box.setTextFormat(Qt::RichText);
    box.setText(text);
    box.exec();
}

void MainWindow::aboutQt()
{
}

void MainWindow::requestMainState(uint32_t pc)
{
    // Do all the "essentials" straight away.
    m_pDispatcher->ReadRegisters();

    // This is the memory for the current instruction.
    // It's used by this and Register windows.
    // *** NOTE this code assumes PC register is already available ***
    m_pDispatcher->ReadMemory(MemorySlot::kMainPC, pc, 10);
    m_pDispatcher->ReadBreakpoints();
    m_pDispatcher->ReadExceptionMask();

    // Basepage makes things much easier
    m_pDispatcher->ReadMemory(MemorySlot::kBasePage, 0, 0x200);

    // Only re-request symbols if we didn't find any the first time
    if (m_pTargetModel->GetSymbolTable().GetHatariSubTable().Count() == 0)
        m_pDispatcher->ReadSymbols();

    m_mainStateUpdateRequest = m_pDispatcher->InsertFlush();
}

void MainWindow::createActions()
{
    // "File"
    m_pLaunchAct = new QAction(tr("&Launch..."), this);
    m_pLaunchAct->setStatusTip(tr("Launch Hatari"));
    m_pLaunchAct->setShortcut(QKeySequence("Alt+L"));
    connect(m_pLaunchAct, &QAction::triggered, this, &MainWindow::LaunchTriggered);

    // "Quicklaunch"
    m_pQuickLaunchAct = new QAction(tr("&QuickLaunch"), this);
    m_pQuickLaunchAct->setStatusTip(tr("Launch Hatari with previous settings"));
    m_pQuickLaunchAct->setShortcut(QKeySequence("Alt+Q"));
    connect(m_pQuickLaunchAct, &QAction::triggered, this, &MainWindow::QuickLaunchTriggered);

    m_pConnectAct = new QAction(tr("&Connect"), this);
    m_pConnectAct->setStatusTip(tr("Connect to Hatari"));
    connect(m_pConnectAct, &QAction::triggered, this, &MainWindow::ConnectTriggered);

    m_pDisconnectAct = new QAction(tr("&Disconnect"), this);
    m_pDisconnectAct->setStatusTip(tr("Disconnect from Hatari"));
    connect(m_pDisconnectAct, &QAction::triggered, this, &MainWindow::DisconnectTriggered);

    m_pWarmResetAct = new QAction(tr("Warm Reset"), this);
    m_pWarmResetAct->setStatusTip(tr("Warm-Reset the machine"));
    connect(m_pWarmResetAct, &QAction::triggered, this, &MainWindow::WarmResetTriggered);

    m_pFastForwardAct = new QAction(tr("Fast-Forward"), this);
    m_pFastForwardAct->setStatusTip(tr("Control Fast-Forward mode"));
    m_pFastForwardAct->setCheckable(true);
    connect(m_pFastForwardAct, &QAction::toggled, this, &MainWindow::FastForwardTriggered);

    m_pExitAct = new QAction(tr("E&xit"), this);
    m_pExitAct->setShortcuts(QKeySequence::Quit);
    m_pExitAct->setStatusTip(tr("Exit the application"));
    connect(m_pExitAct, &QAction::triggered, this, &QWidget::close);

    // Edit
    m_pExceptionsAct = new QAction(tr("&Exceptions..."), this);
    m_pExceptionsAct->setStatusTip(tr("Disconnect from Hatari"));
    connect(m_pExceptionsAct, &QAction::triggered, this, &MainWindow::ExceptionsDialogTriggered);

    m_pPrefsAct = new QAction(tr("&Preferences..."), this);
    m_pPrefsAct->setStatusTip(tr("Set options and preferences"));
    connect(m_pPrefsAct, &QAction::triggered, this, &MainWindow::PrefsDialogTriggered);

    // "Window"
    for (int i = 0; i < kNumDisasmViews; ++i)
    {
        m_pDisasmWindowActs[i] = new QAction(m_pDisasmWidgets[i]->windowTitle(), this);
        m_pDisasmWindowActs[i]->setStatusTip(tr("Show the disassembly window"));
        m_pDisasmWindowActs[i]->setCheckable(true);

        if (i == 0)
            m_pDisasmWindowActs[i]->setShortcut(QKeySequence("Alt+D"));
    }

    for (int i = 0; i < kNumMemoryViews; ++i)
    {
        m_pMemoryWindowActs[i] = new QAction(m_pMemoryViewWidgets[i]->windowTitle(), this);
        m_pMemoryWindowActs[i]->setStatusTip(tr("Show the memory window"));
        m_pMemoryWindowActs[i]->setCheckable(true);

        if (i == 0)
            m_pMemoryWindowActs[i]->setShortcut(QKeySequence("Alt+M"));
    }

    m_pGraphicsInspectorAct = new QAction(tr("&Graphics Inspector"), this);
    m_pGraphicsInspectorAct->setShortcut(QKeySequence("Alt+G"));
    m_pGraphicsInspectorAct->setStatusTip(tr("Show the Graphics Inspector"));
    m_pGraphicsInspectorAct->setCheckable(true);

    m_pBreakpointsWindowAct = new QAction(tr("&Breakpoints"), this);
    m_pBreakpointsWindowAct->setShortcut(QKeySequence("Alt+B"));
    m_pBreakpointsWindowAct->setStatusTip(tr("Show the Breakpoints window"));
    m_pBreakpointsWindowAct->setCheckable(true);

    m_pConsoleWindowAct = new QAction(tr("&Console"), this);
    m_pConsoleWindowAct->setStatusTip(tr("Show the Console window"));
    m_pConsoleWindowAct->setCheckable(true);

    m_pHardwareWindowAct = new QAction(tr("&Hardware"), this);
    m_pHardwareWindowAct->setShortcut(QKeySequence("Alt+H"));
    m_pHardwareWindowAct->setStatusTip(tr("Show the Hardware window"));
    m_pHardwareWindowAct->setCheckable(true);

    m_pProfileWindowAct = new QAction(tr("&Profile"), this);
    m_pProfileWindowAct->setStatusTip(tr("Show the Profile window"));
    m_pProfileWindowAct->setCheckable(true);

    for (int i = 0; i < kNumDisasmViews; ++i)
        connect(m_pDisasmWindowActs[i], &QAction::triggered, this,     [=] () { this->enableVis(m_pDisasmWidgets[i]); m_pDisasmWidgets[i]->keyFocus(); } );

    for (int i = 0; i < kNumMemoryViews; ++i)
        connect(m_pMemoryWindowActs[i], &QAction::triggered, this,     [=] () { this->enableVis(m_pMemoryViewWidgets[i]); m_pMemoryViewWidgets[i]->keyFocus(); } );

    connect(m_pGraphicsInspectorAct, &QAction::triggered, this, [=] () { this->enableVis(m_pGraphicsInspector); m_pGraphicsInspector->keyFocus(); } );
    connect(m_pBreakpointsWindowAct, &QAction::triggered, this, [=] () { this->enableVis(m_pBreakpointsWidget); m_pBreakpointsWidget->keyFocus(); } );
    connect(m_pConsoleWindowAct,     &QAction::triggered, this, [=] () { this->enableVis(m_pConsoleWindow); m_pConsoleWindow->keyFocus(); } );
    connect(m_pHardwareWindowAct,    &QAction::triggered, this, [=] () { this->enableVis(m_pHardwareWindow); m_pHardwareWindow->keyFocus(); } );
    connect(m_pProfileWindowAct,     &QAction::triggered, this, [=] () { this->enableVis(m_pProfileWindow); m_pProfileWindow->keyFocus(); } );

    // "About"
    m_pAboutAct = new QAction(tr("&About"), this);
    m_pAboutAct->setStatusTip(tr("Show the application's About box"));
    connect(m_pAboutAct, &QAction::triggered, this, &MainWindow::about);

    m_pAboutQtAct = new QAction(tr("About &Qt"), this);
    m_pAboutQtAct->setStatusTip(tr("Show the Qt library's About box"));
    connect(m_pAboutQtAct, &QAction::triggered, qApp, &QApplication::aboutQt);
    connect(m_pAboutQtAct, &QAction::triggered, this, &MainWindow::aboutQt);
}

void MainWindow::createToolBar()
{
    QToolBar* pToolbar = new QToolBar(this);
    pToolbar->addAction(m_pQuickLaunchAct);
    pToolbar->addAction(m_pLaunchAct);
    pToolbar->addSeparator();
    pToolbar->addAction(m_pWarmResetAct);
    pToolbar->addAction(m_pFastForwardAct);

    this->addToolBar(Qt::ToolBarArea::TopToolBarArea, pToolbar);
}

void MainWindow::createMenus()
{
    // "File"
    m_pFileMenu = menuBar()->addMenu(tr("&File"));
    m_pFileMenu->addAction(m_pQuickLaunchAct);
    m_pFileMenu->addAction(m_pLaunchAct);
    m_pFileMenu->addAction(m_pConnectAct);
    m_pFileMenu->addAction(m_pDisconnectAct);
    m_pFileMenu->addAction(m_pWarmResetAct);
    m_pFileMenu->addAction(m_pFastForwardAct);
    m_pFileMenu->addSeparator();
    m_pFileMenu->addAction(m_pExitAct);

    m_pEditMenu = menuBar()->addMenu(tr("&Edit"));
    m_pEditMenu->addSeparator();
    m_pEditMenu->addAction(m_pExceptionsAct);
    m_pEditMenu->addSeparator();
    m_pEditMenu->addAction(m_pPrefsAct);

    m_pWindowMenu = menuBar()->addMenu(tr("&Window"));
    for (int i = 0; i < kNumDisasmViews; ++i)
        m_pWindowMenu->addAction(m_pDisasmWindowActs[i]);
    m_pWindowMenu->addSeparator();

    for (int i = 0; i < kNumMemoryViews; ++i)
        m_pWindowMenu->addAction(m_pMemoryWindowActs[i]);
    m_pWindowMenu->addSeparator();

    m_pWindowMenu->addAction(m_pGraphicsInspectorAct);
    m_pWindowMenu->addAction(m_pBreakpointsWindowAct);
    m_pWindowMenu->addAction(m_pConsoleWindowAct);
    m_pWindowMenu->addAction(m_pHardwareWindowAct);
    m_pWindowMenu->addAction(m_pProfileWindowAct);

    m_pHelpMenu = menuBar()->addMenu(tr("Help"));
    m_pHelpMenu->addAction(m_pAboutAct);
    m_pHelpMenu->addAction(m_pAboutQtAct);
}

void MainWindow::enableVis(QWidget* pWidget)
{
    // This used to be a toggle
    pWidget->setVisible(true);
    pWidget->setHidden(false);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    event->accept();
}
