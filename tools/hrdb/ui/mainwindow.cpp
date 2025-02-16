#include "mainwindow.h"

#include <iostream>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QToolBar>

#include "hopper/buffer.h"
#include "hopper56/buffer.h"

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../models/session.h"
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

static int ShowResetWarning()
{
    QMessageBox msgBox;
    msgBox.setText("Reset Hatari?");
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setInformativeText("All memory data will be lost.");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    int ret = msgBox.exec();
    return ret;
}

MainWindow::MainWindow(Session& session, QWidget *parent)
    : QMainWindow(parent),
      m_session(session),
      m_mainStateStartedRequest(0),
      m_mainStateCompleteRequest(0),
      m_liveRegisterReadRequest(0)
{
    setObjectName("MainWindow");
    m_pTargetModel = m_session.m_pTargetModel;
    m_pDispatcher = m_session.m_pDispatcher;

    // Creation - done in Tab order
    // Register/status window
    m_pRegisterWidget = new RegisterWidget(this, &m_session);

    QScrollArea* pScrollArea = new QScrollArea(this);
    pScrollArea->setWidget(m_pRegisterWidget);
    pScrollArea->setWidgetResizable(false);

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
            "Ctrl+S: skip instruction.");
    m_pStepOverButton->setToolTip("N: Stop at next instruction in memory.\n"
            "Jumps over subroutines and through backwards loops.");

    m_pRunToButton = new QPushButton("Run (U)ntil:", this);
    m_pRunToButton->setToolTip("U: Run until specified condition");

    m_pRunToCombo = new QComboBox(this);
    m_pRunToCombo->insertItem(kRunToRts, "RTS");
    m_pRunToCombo->insertItem(kRunToRte, "RTE");
    m_pRunToCombo->insertItem(kRunToVbl, "Next VBL");
    m_pRunToCombo->insertItem(kRunToHbl, "Next HBL");
    m_pRunToCombo->insertItem(kRunToRam, "In RAM");

    m_pDspStepIntoButton = new QPushButton("Step DSP", this);
    m_pDspStepOverButton = new QPushButton("Next DSP", this);
    m_pDspStepIntoButton->setToolTip("Shift+S: Execute one DSP instruction.\n"
            "Jumps into subroutines.\n");
    m_pDspStepOverButton->setToolTip("Shift+N: Stop at next instruction in memory.\n"
            "Jumps over subroutines and through loop-to-self.");

    for (int i = 0; i < kNumDisasmViews; ++i)
    {
        m_pDisasmWidgets[i] = new DisasmWindow(this, &m_session, i);
        if (i == 0)
            m_pDisasmWidgets[i]->setWindowTitle("Disassembly 1 (Alt+D)");
        else
            m_pDisasmWidgets[i]->setWindowTitle(QString::asprintf("Disassembly %d", i + 1));
    }

    static const char* windowTitles[4] =
    {
        "Memory 1 (Alt+M)",
        "Memory 2 (Alt+2)",
        "Memory 3 (Alt+3)",
        "Memory 4 (Alt+4)"
    };

    for (int i = 0; i < kNumMemoryViews; ++i)
    {
        m_pMemoryViewWidgets[i] = new MemoryWindow(this, &m_session, i);
        m_pMemoryViewWidgets[i]->setWindowTitle(windowTitles[i]);
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
    QHBoxLayout *hlayoutTop = new QHBoxLayout;
    QHBoxLayout *hlayoutDsp = new QHBoxLayout;
    auto pTopGroupBox = new QWidget(this);
    m_pDspTopWidget = new QWidget(this);

    auto pMainGroupBox = new QGroupBox(this);

    SetMargins(hlayoutTop);
    hlayoutTop->setAlignment(Qt::AlignLeft);
    hlayoutTop->addWidget(m_pRunningSquare);
    hlayoutTop->addWidget(m_pStartStopButton);
    hlayoutTop->addWidget(m_pStepIntoButton);
    hlayoutTop->addWidget(m_pStepOverButton);
    hlayoutTop->addWidget(m_pRunToButton);
    hlayoutTop->addWidget(m_pRunToCombo);
    hlayoutTop->addStretch();
    //hlayout->setAlignment(m_pRunToCombo, Qt::Align);
    pTopGroupBox->setLayout(hlayoutTop);

    SetMargins(hlayoutDsp);
    hlayoutDsp->setAlignment(Qt::AlignLeft);
    hlayoutDsp->addWidget(m_pDspStepIntoButton);
    hlayoutDsp->addWidget(m_pDspStepOverButton);
    hlayoutDsp->addStretch();
    m_pDspTopWidget->setLayout(hlayoutDsp);

    SetMargins(vlayout);
    vlayout->addWidget(pTopGroupBox);
    vlayout->addWidget(m_pDspTopWidget);
    vlayout->addWidget(pScrollArea);
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
    connect(m_pTargetModel, &TargetModel::startStopChangedSignal,    this, &MainWindow::startStopChanged);
    connect(m_pTargetModel, &TargetModel::configChangedSignal,       this, &MainWindow::configChanged);
    connect(m_pTargetModel, &TargetModel::connectChangedSignal,      this, &MainWindow::connectChanged);
    connect(m_pTargetModel, &TargetModel::memoryChangedSignal,       this, &MainWindow::memoryChanged);
    connect(m_pTargetModel, &TargetModel::runningRefreshTimerSignal, this, &MainWindow::runningRefreshTimer);
    connect(m_pTargetModel, &TargetModel::flushSignal,               this, &MainWindow::flush);
    connect(m_pTargetModel, &TargetModel::protocolMismatchSignal,    this, &MainWindow::protocolMismatch);
    connect(m_pTargetModel, &TargetModel::saveBinCompleteSignal,     this, &MainWindow::saveBinComplete);
    connect(m_pTargetModel, &TargetModel::symbolProgramChangedSignal,this, &MainWindow::symbolProgramChanged);

    // Wire up buttons to actions
    connect(m_pStartStopButton,   &QAbstractButton::clicked, this, &MainWindow::startStopClickedSlot);
    connect(m_pStepIntoButton,    &QAbstractButton::clicked, this, &MainWindow::singleStepClickedSlot);
    connect(m_pStepOverButton,    &QAbstractButton::clicked, this, &MainWindow::nextClickedSlot);
    connect(m_pRunToButton,       &QAbstractButton::clicked, this, &MainWindow::runToClickedSlot);

    connect(m_pDspStepIntoButton, &QAbstractButton::clicked, this, &MainWindow::singleStepDspClickedSlot);
    connect(m_pDspStepOverButton, &QAbstractButton::clicked, this, &MainWindow::nextDspClickedSlot);

    // Wire up menu appearance
    connect(m_pWindowMenu, &QMenu::aboutToShow,            this, &MainWindow::updateWindowMenu);

    // Status bar
    connect(&m_session,    &Session::messageSet, this, &MainWindow::messageSet);

    // Keyboard shortcuts
    new QShortcut(QKeySequence("Ctrl+R"),         this, SLOT(startStopClickedSlot()));
    new QShortcut(QKeySequence("Esc"),            this, SLOT(breakPressedSlot()));
    new QShortcut(QKeySequence("S"),              this, SLOT(singleStepClickedSlot()));
    new QShortcut(QKeySequence("Shift+S"),        this, SLOT(singleStepDspClickedSlot()));
    new QShortcut(QKeySequence("Ctrl+S"),         this, SLOT(skipPressedSlot()));
    new QShortcut(QKeySequence("N"),              this, SLOT(nextClickedSlot()));
    new QShortcut(QKeySequence("Shift+N"),        this, SLOT(nextDspClickedSlot()));
    new QShortcut(QKeySequence("U"),              this, SLOT(runToClickedSlot()));
    new QShortcut(QKeySequence("Ctrl+Shift+U"),   this, SLOT(cycleRunToSlot()));
    // Specific "Run until" modes
    new QShortcut(QKeySequence("Ctrl+U,S"),       this, SLOT(runToRtsSlot()));
    new QShortcut(QKeySequence("Ctrl+U,E"),       this, SLOT(runToRteSlot()));
    new QShortcut(QKeySequence("Ctrl+U,V"),       this, SLOT(runToVblSlot()));
    new QShortcut(QKeySequence("Ctrl+U,H"),       this, SLOT(runToHblSlot()));
    new QShortcut(QKeySequence("Ctrl+U,R"),       this, SLOT(runToRamSlot()));

    new QShortcut(QKeySequence("F5"),            this, SLOT(startStopClickedSlot()));
    new QShortcut(QKeySequence("F11"),           this, SLOT(singleStepClickedSlot()));
    new QShortcut(QKeySequence("F10"),           this, SLOT(nextClickedSlot()));

    // Try initial connect
    ConnectTriggered();

    // Update everything to ensure UI elements are up-to-date
    configChanged();
    connectChanged();
    startStopChanged();

    this->statusBar()->showMessage("Welcome to hrdb, version " VERSION_STRING, 0);
}

MainWindow::~MainWindow()
{
    delete m_pDispatcher;
    delete m_pTargetModel;
}

void MainWindow::connectChanged()
{
    PopulateRunningSquare();
    updateButtonEnable();

    //if (m_pTargetModel->IsConnected())
    //    m_pDispatcher->SendCommandPacket("profile 1");
}

void MainWindow::configChanged()
{
    m_pDspTopWidget->setVisible(m_pTargetModel->IsDspActive());
}

void MainWindow::startStopChanged()
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
        requestMainState(m_pTargetModel->GetStartStopPC(kProcCpu));
    }
    PopulateRunningSquare();
    updateButtonEnable();
}

void MainWindow::memoryChanged(int slot, uint64_t /*commandId*/)
{
    if (slot == MemorySlot::kMainPC)
    {
        // Disassemble the first instruction
        m_disasm.lines.clear();
        const Memory* pMem = m_pTargetModel->GetMemory(MemorySlot::kMainPC);
        if (pMem)
        {
            // Fetch data and decode the next instruction.
            hop68::buffer_reader disasmBuf(pMem->GetData(), pMem->GetSize(), pMem->GetAddress());
            Disassembler::decode_buf(disasmBuf, m_disasm, m_pTargetModel->GetDisasmSettings(), pMem->GetAddress(), 1);
        }
    }
    if (slot == MemorySlot::kMainDspPC)
    {
        // Disassemble the first instruction
        m_disasm56.lines.clear();
        const Memory* pMem = m_pTargetModel->GetMemory(MemorySlot::kMainDspPC);
        if (pMem)
        {
            // Fetch data and decode the next instruction.
            hop56::buffer_reader disasmBuf(pMem->GetData(), pMem->GetSize(), pMem->GetAddress());
            hop56::decode_settings dummy;
            Disassembler56::decode_buf(disasmBuf, m_disasm56, dummy, pMem->GetAddress(), 1);
        }
    }
    // Flagging main state update end is now handled by Flush()
 }

void MainWindow::runningRefreshTimer()
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

void MainWindow::flush(const TargetChangedFlags& /*flags*/, uint64_t commandId)
{
    if (commandId == m_liveRegisterReadRequest)
    {
        // Now that we have registers from the live request, get disassmbly memory
        // using the full register bank
        requestMainState(m_pTargetModel->GetRegs().Get(Registers::PC));
        m_liveRegisterReadRequest = 0;
    }
    else if (commandId == m_mainStateStartedRequest)
    {
        m_pTargetModel->SetMainUpdate(true);
        m_mainStateStartedRequest = 0;
    }
    else if (commandId == m_mainStateCompleteRequest)
    {
        // This is where we should flag completion
        m_pTargetModel->SetMainUpdate(false);
        m_mainStateCompleteRequest = 0;
    }
}

void MainWindow::protocolMismatch(uint32_t hatariProtocol, uint32_t hrdbProtocol)
{
    // Ensure that auto-connect gets turned off!
    m_session.Disconnect();
    QString text;
    QTextStream ref(&text);
    ref.setIntegerBase(16);
    ref << "Protocol version mismatch:\n";
    ref << "\nHatari protocol version is: 0x" << hatariProtocol;
    ref << "\nbut hrdb expects version: 0x" << hrdbProtocol;
    QMessageBox box(QMessageBox::Critical, "Can't connect", text);
    box.exec();
}

void MainWindow::saveBinComplete(uint64_t /*commandId*/, uint32_t errorCode)
{
    if (!errorCode)
        messageSet("File saved.");
    else
        messageSet(QString::asprintf("Unable to save file (error %d)", errorCode));
}

void MainWindow::symbolProgramChanged()
{
    m_pDispatcher->ReadSymbols();
}

void MainWindow::startStopClickedSlot()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_pTargetModel->IsRunning())
        m_pDispatcher->Break();
    else
        m_pDispatcher->Run();
}

void MainWindow::singleStepClickedSlot()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_pTargetModel->IsRunning())
        return;

    m_pDispatcher->Step(kProcCpu);
}

void MainWindow::singleStepDspClickedSlot()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_pTargetModel->IsRunning())
        return;

    m_pDispatcher->Step(kProcDsp);
}

void MainWindow::nextClickedSlot()
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
    if(m_disasm.lines[0].address != m_pTargetModel->GetStartStopPC(kProcCpu))
        return;

    const Disassembler::line& nextInst = m_disasm.lines[0];
    // Either "next" or set breakpoint to following instruction
    bool shouldStepOver = DisAnalyse::isSubroutine(nextInst.inst) ||
                          DisAnalyse::isTrap(nextInst.inst) ||
                          DisAnalyse::isBackDbf(nextInst.inst);
    if (shouldStepOver)
    {
        uint32_t next_pc = nextInst.inst.byte_count + nextInst.address;
        m_pDispatcher->RunToPC(kProcCpu, next_pc);
    }
    else
    {
        m_pDispatcher->Step(kProcCpu);
    }
}

void MainWindow::nextDspClickedSlot()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_pTargetModel->IsRunning())
        return;

    // Work out where the next PC is
    if (m_disasm56.lines.size() == 0)
        return;

    // Bug fix: we can't decide on how to step until the available disassembly matches
    // the PC we are stepping from. This slows down stepping a little (since there is
    // a round-trip). In theory we could send the next instruction opcode as part of
    // the "status" notification if we want it to be faster.

    uint32_t pc = m_pTargetModel->GetStartStopPC(kProcDsp);
    if(m_disasm56.lines[0].address != pc)
        return;

    const Disassembler56::line& currLine = m_disasm56.lines[0];
    // Either "next" or set breakpoint to following instruction
    bool shouldStepOver = DisAnalyse56::isSubroutine(currLine.inst);

    // Step over branches-to-self too
    // TODO needs better "step over" logic here, possibly a specific function.
    uint32_t targetAddr = -1;
    bool reversed = false;
    bool isBranch = DisAnalyse56::getBranchTarget(currLine.inst, currLine.address, targetAddr, reversed);
    if (isBranch && !reversed && targetAddr == currLine.address)
        shouldStepOver = true;

    if (shouldStepOver)
    {
        uint32_t next_pc = currLine.inst.word_count + currLine.address;
        m_pDispatcher->RunToPC(kProcDsp, next_pc);
    }
    else
    {
        m_pDispatcher->Step(kProcDsp);
    }
}

void MainWindow::skipPressedSlot()
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
    if(m_disasm.lines[0].address != m_pTargetModel->GetStartStopPC(kProcCpu))
        return;

    const Disassembler::line& nextInst = m_disasm.lines[0];
    m_pDispatcher->SetRegister(kProcCpu, Registers::PC, nextInst.GetEnd());
}

void MainWindow::runToClickedSlot()
{
    if (!m_pTargetModel->IsConnected())
        return;
    if (m_pTargetModel->IsRunning())
        return;

    runTo((RunToMode)m_pRunToCombo->currentIndex());
}

void MainWindow::cycleRunToSlot()
{
    int current = m_pRunToCombo->currentIndex();
    current++;
    current %= kRunToMax;
    m_pRunToCombo->setCurrentIndex(current);
}

void MainWindow::addBreakpointPressed()
{
    AddBreakpointDialog dialog(this, m_pTargetModel, m_pDispatcher);
    dialog.exec();
}

void MainWindow::breakPressedSlot()
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
    int ret = ShowResetWarning();
    // Use the shared session call for this, which handles
    // things like symbol loading
    if (ret == QMessageBox::Yes)
        m_session.resetWarm();
}

void MainWindow::ColdResetTriggered()
{
    int ret = ShowResetWarning();
    // Use the shared session call for this, which handles
    // things like symbol loading
    if (ret == QMessageBox::Yes)
        m_session.resetCold();
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
    pal.setColor(QPalette::Window, col);
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
    bool dspActive = m_pTargetModel->IsDspActive();

    // Buttons...
    m_pStartStopButton->setEnabled(isConnected);
    m_pStartStopButton->setText(isRunning ? "Break" : "Run");
    m_pStepIntoButton->setEnabled(isConnected && !isRunning);
    m_pStepOverButton->setEnabled(isConnected && !isRunning);
    m_pRunToButton->setEnabled(isConnected && !isRunning);

    m_pDspStepIntoButton->setEnabled(dspActive && isConnected && !isRunning);
    m_pDspStepOverButton->setEnabled(dspActive && isConnected && !isRunning);

    // Menu items...
    m_pConnectAct->setEnabled(!isConnected);
    m_pDisconnectAct->setEnabled(isConnected);
    m_pWarmResetAct->setEnabled(isConnected);
    m_pColdResetAct->setEnabled(isConnected);
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
    m_pRegisterWidget->saveSettings();
}

void MainWindow::runTo(MainWindow::RunToMode mode)
{
    if (!m_pTargetModel->IsConnected())
        return;
    if (m_pTargetModel->IsRunning())
        return;

    if (mode == kRunToRts)
        m_pDispatcher->SetBreakpoint(kProcCpu, "(pc).w = $4e75", Dispatcher::kBpFlagOnce);   // RTS
    else if (mode == kRunToRte)
        m_pDispatcher->SetBreakpoint(kProcCpu, "(pc).w = $4e73", Dispatcher::kBpFlagOnce);   // RTE
    else if (mode == kRunToVbl)
        m_pDispatcher->SetBreakpoint(kProcCpu, "VBL ! VBL", Dispatcher::kBpFlagOnce);        // VBL
    else if (mode == kRunToHbl)
        m_pDispatcher->SetBreakpoint(kProcCpu, "HBL ! HBL", Dispatcher::kBpFlagOnce);        // VBL
    else if (mode == kRunToRam)
        m_pDispatcher->SetBreakpoint(kProcCpu, "PC < $e00000", Dispatcher::kBpFlagOnce);     // Not in TOS
    else
        return;
    // start execution again
    m_pDispatcher->Run();
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

void MainWindow::onlineHelp()
{
    QDesktopServices::openUrl(QUrl(HELP_URL));
}

void MainWindow::messageSet(const QString &msg)
{
    this->statusBar()->showMessage(msg, 5000);
}

void MainWindow::requestMainState(uint32_t pc)
{
    // Insert flag for the start of main state updating
    m_mainStateStartedRequest = m_pDispatcher->InsertFlush();

    // Do all the "essentials" straight away.
    m_pDispatcher->ReadRegisters();

    // This is the memory for the current instruction.
    // It's used by this and Register windows.
    // *** NOTE this code assumes PC register is already available ***
    m_pDispatcher->ReadMemory(MemorySlot::kMainPC, MEM_CPU, pc, 32);
    m_pDispatcher->ReadBreakpoints();
    m_pDispatcher->ReadExceptionMask();
    if (m_pTargetModel->IsDspActive())
        m_pDispatcher->ReadMemory(MemorySlot::kMainDspPC, MEM_P, m_pTargetModel->GetStartStopPC(kProcDsp), 2U);

    // Basepage makes things much easier
    m_pDispatcher->ReadMemory(MemorySlot::kBasePage, 0, 0x200);
    m_mainStateCompleteRequest = m_pDispatcher->InsertFlush();
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

    m_pColdResetAct = new QAction(tr("Cold Reset"), this);
    m_pColdResetAct->setStatusTip(tr("Cold-Reset the machine"));
    connect(m_pColdResetAct, &QAction::triggered, this, &MainWindow::ColdResetTriggered);

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

    static const char* windowShortcuts[4] =
    {
        "Alt+M",
        "Alt+2",
        "Alt+3",
        "Alt+4"
    };
    for (int i = 0; i < kNumMemoryViews; ++i)
    {
        m_pMemoryWindowActs[i] = new QAction(m_pMemoryViewWidgets[i]->windowTitle(), this);
        m_pMemoryWindowActs[i]->setStatusTip(tr("Show the memory window"));
        m_pMemoryWindowActs[i]->setCheckable(true);
        m_pMemoryWindowActs[i]->setShortcut(QKeySequence(windowShortcuts[i]));
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
    m_pConsoleWindowAct->setShortcut(QKeySequence("Alt+C"));
    m_pConsoleWindowAct->setStatusTip(tr("Show the Console window"));
    m_pConsoleWindowAct->setCheckable(true);

    m_pHardwareWindowAct = new QAction(tr("&Hardware"), this);
    m_pHardwareWindowAct->setShortcut(QKeySequence("Alt+H"));
    m_pHardwareWindowAct->setStatusTip(tr("Show the Hardware window"));
    m_pHardwareWindowAct->setCheckable(true);

    m_pProfileWindowAct = new QAction(tr("&Profile"), this);
    m_pProfileWindowAct->setShortcut(QKeySequence("Alt+P"));
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

    // "About"
    m_pOnlineHelpAct = new QAction(tr("Online Help"), this);
    m_pOnlineHelpAct->setStatusTip(tr("Access online help page"));
    connect(m_pOnlineHelpAct, &QAction::triggered, this, &MainWindow::onlineHelp);

    m_pAboutQtAct = new QAction(tr("About &Qt"), this);
    m_pAboutQtAct->setStatusTip(tr("Show the Qt library's About box"));
    connect(m_pAboutQtAct, &QAction::triggered, qApp, &QApplication::aboutQt);
    connect(m_pAboutQtAct, &QAction::triggered, this, &MainWindow::aboutQt);
}

void MainWindow::createToolBar()
{
    QToolBar* pToolbar = new QToolBar(this);
    pToolbar->setObjectName("MainToolbar");
    pToolbar->addAction(m_pQuickLaunchAct);
    pToolbar->addAction(m_pLaunchAct);
    pToolbar->addSeparator();
    pToolbar->addAction(m_pColdResetAct);
    pToolbar->addAction(m_pWarmResetAct);
    pToolbar->addSeparator();
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
    m_pFileMenu->addSeparator();
    m_pFileMenu->addAction(m_pColdResetAct);
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
    m_pHelpMenu->addAction(m_pOnlineHelpAct);
    m_pHelpMenu->addAction(m_pAboutAct);
    m_pHelpMenu->addAction(m_pAboutQtAct);
}

void MainWindow::enableVis(QDockWidget* pWidget)
{
    // This used to be a toggle
    pWidget->setVisible(true);
    //pWidget->activateWindow();
    // I took 2 years to find this!
    // https://stackoverflow.com/questions/1290882/focusing-on-a-tabified-qdockwidget-in-pyqt
    pWidget->raise();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    event->accept();
}
