#include "mainwindow.h"

#include <iostream>
#include <QtWidgets>
#include <QtNetwork>
#include <QShortcut>
#include <QFontDatabase>

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../models/exceptionmask.h"

#include "disasmwidget.h"
#include "memoryviewwidget.h"
#include "graphicsinspector.h"
#include "breakpointswidget.h"
#include "addbreakpointdialog.h"
#include "exceptiondialog.h"
#include "rundialog.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , tcpSocket(new QTcpSocket(this))
{
    setObjectName("MainWindow");

    // Create the core data models, since other object want to connect to them.
    m_pTargetModel = new TargetModel();
    m_pDispatcher = new Dispatcher(tcpSocket, m_pTargetModel);

    // Top row of buttons
    m_pRunningSquare = new QWidget(this);
    m_pRunningSquare->setFixedSize(10, 25);
    m_pRunningSquare->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));
    m_pStartStopButton = new QPushButton("Break", this);
    m_pStepIntoButton = new QPushButton("Step", this);
    m_pStepOverButton = new QPushButton("Next", this);
    m_pRunToButton = new QPushButton("Run Until:", this);
    m_pRunToCombo = new QComboBox(this);
    m_pRunToCombo->insertItem(0, "RTS");
    m_pRunToCombo->insertItem(1, "RTE");
    m_pRunToCombo->insertItem(2, "Next VBL");
    m_pRunToCombo->insertItem(3, "Next HBL");

    // Register/status window
    m_pRegistersTextEdit = new QTextEdit("", this);
	m_pRegistersTextEdit->setReadOnly(true);
	m_pRegistersTextEdit->setAcceptRichText(false);

    const QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_pRegistersTextEdit->setCurrentFont(monoFont);
    m_pRegistersTextEdit->setLineWrapMode(QTextEdit::LineWrapMode::NoWrap);
    m_pRegistersTextEdit->setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);

    m_pDisasmWidget0 = new DisasmViewWidget(this, m_pTargetModel, m_pDispatcher, 0);
    m_pDisasmWidget0->setWindowTitle("Disassembly (Alt+D)");
    m_pDisasmWidget1 = new DisasmViewWidget(this, m_pTargetModel, m_pDispatcher, 1);
    m_pDisasmWidget1->setWindowTitle("Disassembly 2");
    m_pMemoryViewWidget0 = new MemoryViewWidget(this, m_pTargetModel, m_pDispatcher, 0);
    m_pMemoryViewWidget0->setWindowTitle("Memory (Alt+M)");
    m_pMemoryViewWidget1 = new MemoryViewWidget(this, m_pTargetModel, m_pDispatcher, 1);
    m_pMemoryViewWidget1->setWindowTitle("Memory 2");
    m_pGraphicsInspector = new GraphicsInspectorWidget(this, m_pTargetModel, m_pDispatcher);
    m_pGraphicsInspector->setWindowTitle("Graphics Inspector (Alt+G)");
    m_pBreakpointsWidget = new BreakpointsWidget(this, m_pTargetModel, m_pDispatcher);
    m_pBreakpointsWidget->setWindowTitle("Breakpoints (Alt+B)");
    m_pExceptionDialog = new ExceptionDialog(this, m_pTargetModel, m_pDispatcher);
    m_pRunDialog = new RunDialog(this, m_pTargetModel, m_pDispatcher);

    // https://doc.qt.io/qt-5/qtwidgets-layouts-basiclayouts-example.html
    QVBoxLayout *vlayout = new QVBoxLayout;
    QHBoxLayout *hlayout = new QHBoxLayout;
    auto pTopGroupBox = new QWidget(this);
    auto pMainGroupBox = new QGroupBox(this);

    hlayout->addWidget(m_pRunningSquare);
    hlayout->addWidget(m_pStartStopButton);
    hlayout->addWidget(m_pStepIntoButton);
    hlayout->addWidget(m_pStepOverButton);
    hlayout->addWidget(m_pRunToButton);
    hlayout->addWidget(m_pRunToCombo);

    //hlayout->setAlignment(m_pRunToCombo, Qt::Align);
    pTopGroupBox->setLayout(hlayout);

    vlayout->addWidget(pTopGroupBox);
    vlayout->addWidget(m_pRegistersTextEdit);
    vlayout->setAlignment(Qt::Alignment(Qt::AlignTop));
    pMainGroupBox->setFlat(true);
    pMainGroupBox->setLayout(vlayout);

    setCentralWidget(pMainGroupBox);

    this->addDockWidget(Qt::BottomDockWidgetArea, m_pDisasmWidget0);
    this->addDockWidget(Qt::RightDockWidgetArea, m_pMemoryViewWidget0);
    this->addDockWidget(Qt::BottomDockWidgetArea, m_pMemoryViewWidget1);
    this->addDockWidget(Qt::BottomDockWidgetArea, m_pDisasmWidget1);
    this->addDockWidget(Qt::LeftDockWidgetArea, m_pGraphicsInspector);
    this->addDockWidget(Qt::BottomDockWidgetArea, m_pBreakpointsWidget);

    loadSettings();

    // Set up menus (reflecting current state)
    createActions();
    createMenus();

    // Listen for target changes
    connect(m_pTargetModel, &TargetModel::startStopChangedSignal, this, &MainWindow::startStopChangedSlot);
    connect(m_pTargetModel, &TargetModel::registersChangedSignal, this, &MainWindow::registersChangedSlot);
    connect(m_pTargetModel, &TargetModel::connectChangedSignal,   this, &MainWindow::connectChangedSlot);
    connect(m_pTargetModel, &TargetModel::memoryChangedSignal,    this, &MainWindow::memoryChangedSlot);
    connect(m_pTargetModel, &TargetModel::symbolTableChangedSignal,this, &MainWindow::symbolTableChangedSlot);
    connect(m_pTargetModel, &TargetModel::startStopChangedSignalDelayed,this, &MainWindow::startStopDelayedSlot);

    // Wire up cross-window requests
    connect(m_pTargetModel, &TargetModel::addressRequested, m_pMemoryViewWidget0, &MemoryViewWidget::requestAddress);
    connect(m_pTargetModel, &TargetModel::addressRequested, m_pMemoryViewWidget1, &MemoryViewWidget::requestAddress);
    connect(m_pTargetModel, &TargetModel::addressRequested, m_pDisasmWidget0,     &DisasmViewWidget::requestAddress);
    connect(m_pTargetModel, &TargetModel::addressRequested, m_pDisasmWidget1,     &DisasmViewWidget::requestAddress);

    // Wire up buttons to actions
    connect(m_pStartStopButton, &QAbstractButton::clicked, this, &MainWindow::startStopClicked);
    connect(m_pStepIntoButton, &QAbstractButton::clicked, this, &MainWindow::singleStepClicked);
    connect(m_pStepOverButton, &QAbstractButton::clicked, this, &MainWindow::nextClicked);
    connect(m_pRunToButton, &QAbstractButton::clicked, this, &MainWindow::runToClicked);

    // Wire up menu appearance
    connect(windowMenu, &QMenu::aboutToShow, this, &MainWindow::updateWindowMenu);

	// Keyboard shortcuts
    new QShortcut(QKeySequence(tr("Ctrl+R", "Start/Stop")),         this, SLOT(startStopClicked()));
    new QShortcut(QKeySequence(tr("n",      "Next")),               this, SLOT(nextClicked()));
    new QShortcut(QKeySequence(tr("s",      "Step")),               this, SLOT(singleStepClicked()));
    new QShortcut(QKeySequence(tr("Esc",    "Break")),              this, SLOT(breakPressed()));
    new QShortcut(QKeySequence(tr("u",      "Run Until")),          this, SLOT(runToClicked()));

    // Try initial connect
    Connect();

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
    bool isConnect = m_pTargetModel->IsConnected() ? true : false;
    m_pRegistersTextEdit->setEnabled(isConnect);

    PopulateRunningSquare();
    PopulateRegisters();
    updateButtonEnable();
}

void MainWindow::startStopChangedSlot()
{
    bool isRunning = m_pTargetModel->IsRunning();

    // Update text here
    if (isRunning)
	{       
        // Update our previous values
        // TODO: this is not the ideal way to do this, since there
        // are timing issues. In future, parcel up the stopping updates so
        // that widget refreshes happen as one.
        m_prevRegs = m_pTargetModel->GetRegs();
    }
	else
	{
        // STOPPED
		// TODO this is where all windows should put in requests for data

        // Do all the "essentials" straight away.
        m_pDispatcher->SendCommandPacket("regs");
        m_pDispatcher->RequestMemory(MemorySlot::kMainPC, m_pTargetModel->GetPC(), 10);

        m_pDispatcher->SendCommandPacket("bplist");
        m_pDispatcher->SendCommandPacket("exmask");

        // Video memory is generally handy
        m_pDispatcher->RequestMemory(MemorySlot::kVideo, 0xff8200, 0x70);

        // Only re-request symbols if we didn't find any the first time
        if (m_pTargetModel->GetSymbolTable().m_userSymbolCount == 0)  // NO CHECK
            m_pDispatcher->SendCommandPacket("symlist");

        m_pRegistersTextEdit->setEnabled(true);
    }
    PopulateRunningSquare();
    PopulateRegisters();

    updateButtonEnable();
}

void MainWindow::startStopDelayedSlot(int running)
{
    if (running)
    {
        m_pRegistersTextEdit->setEnabled(false);
        m_pRegistersTextEdit->setText("Running, Ctrl+R to break...");
    }
}

void MainWindow::registersChangedSlot(uint64_t /*commandId*/)
{
	// Update text here
    PopulateRegisters();
}

void MainWindow::memoryChangedSlot(int slot, uint64_t /*commandId*/)
{
    if (slot != MemorySlot::kMainPC)
        return;

    // Disassemble the first instruction
    m_disasm.lines.clear();
    const Memory* pMem = m_pTargetModel->GetMemory(MemorySlot::kMainPC);
    if (!pMem)
        return;

    // Fetch underlying data, this is picked up by the model class
    buffer_reader disasmBuf(pMem->GetData(), pMem->GetSize());
    Disassembler::decode_buf(disasmBuf, m_disasm, pMem->GetAddress(), 2);
    PopulateRegisters();
}

void MainWindow::symbolTableChangedSlot(uint64_t /*commandId*/)
{
    PopulateRegisters();
}

void MainWindow::startStopClicked()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_pTargetModel->IsRunning())
        m_pDispatcher->SendCommandPacket("break");
	else
        m_pDispatcher->SendCommandPacket("run");
}

void MainWindow::singleStepClicked()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_pTargetModel->IsRunning())
        return;
    m_pDispatcher->SendCommandPacket("step");
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

    const Disassembler::line& nextInst = m_disasm.lines[0];
    // Either "next" or set breakpoint to following instruction
    bool shouldStepOver = DisAnalyse::isSubroutine(nextInst.inst) ||
                          DisAnalyse::isTrap(nextInst.inst);
    if (shouldStepOver)
    {
        uint32_t next_pc = nextInst.inst.byte_count + nextInst.address;
        m_pDispatcher->RunToPC(next_pc);
    }
    else
    {
        m_pDispatcher->SendCommandPacket("step");
    }
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
    m_pDispatcher->SendCommandPacket("run");
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
        m_pDispatcher->SendCommandPacket("break");
}

// Actions
void MainWindow::Run()
{
    m_pRunDialog->setModal(true);
    m_pRunDialog->show();
}

void MainWindow::Connect()
{
    // Create the TCP socket and start listening
    QHostAddress qha(QHostAddress::LocalHost);
    tcpSocket->connectToHost(qha, 56001);
}

void MainWindow::Disconnect()
{
    tcpSocket->disconnectFromHost();
}

void MainWindow::ExceptionsDialog()
{
    m_pExceptionDialog->setModal(true);
    m_pExceptionDialog->show();
}

QString DispReg16(int regIndex, const Registers& prevRegs, const Registers& regs)
{
    const char* col = (regs.m_value[regIndex] != prevRegs.m_value[regIndex]) ? "red" : "black";
    return QString::asprintf("%s: <span style=\"color:%s\">%04x</span>", Registers::s_names[regIndex], col, regs.m_value[regIndex]);
}
QString DispReg32(int regIndex, const Registers& prevRegs, const Registers& regs)
{
    const char* col = (regs.m_value[regIndex] != prevRegs.m_value[regIndex]) ? "red" : "black";
    return QString::asprintf("%s: <span style=\"color:%s\">%08x</span>", Registers::s_names[regIndex], col, regs.m_value[regIndex]);
}

QString DispSR(const Registers& prevRegs, const Registers& regs, uint32_t bit, const char* pName)
{
	uint32_t mask = 1U << bit;
	uint32_t valNew = regs.m_value[Registers::SR] & mask;
	uint32_t valOld = prevRegs.m_value[Registers::SR] & mask;

    const char* col = valNew != valOld ? "red" : "black";
	const char* text = valNew != 0 ? pName : ".";
    return QString::asprintf("<span style=\"color:%s\">%s</span>", col, text);
}

void MainWindow::PopulateRegisters()
{
    if (!m_pTargetModel->IsConnected())
    {
        m_pRegistersTextEdit->clear();
        return;
    }

    if (m_pTargetModel->IsRunning())
		return;

	// Build up the text area
	QString regsText;
    QTextStream ref(&regsText);

	Registers regs = m_pTargetModel->GetRegs();

    ref << "<pre>";

    ref << DispReg32(Registers::PC, m_prevRegs, regs) << "   ";
    if (m_disasm.lines.size() > 0)
        Disassembler::print(m_disasm.lines[0].inst, m_disasm.lines[0].address, ref);
    ref << "     ;" << FindSymbol(GET_REG(regs, PC) & 0xffffff);
    ref << "<br>";
    ref << DispReg16(Registers::SR, m_prevRegs, regs) << "   ";
	ref << DispSR(m_prevRegs, regs, 15, "T");
	ref << DispSR(m_prevRegs, regs, 14, "T");
	ref << " ";
	ref << DispSR(m_prevRegs, regs, 13, "S");
	ref << " ";
	ref << DispSR(m_prevRegs, regs, 10, "2");
	ref << DispSR(m_prevRegs, regs, 9, "1");
	ref << DispSR(m_prevRegs, regs, 8, "0");
	ref << " ";
	ref << DispSR(m_prevRegs, regs, 4, "X");
	ref << DispSR(m_prevRegs, regs, 3, "N");
	ref << DispSR(m_prevRegs, regs, 2, "Z");
	ref << DispSR(m_prevRegs, regs, 1, "V");
	ref << DispSR(m_prevRegs, regs, 0, "C");

    uint32_t ex = GET_REG(regs, EX);
    if (ex != 0)
        ref << "<br>" << "EXCEPTION: " << ExceptionMask::GetName(ex);

    ref << "<br><br>";
    ref << DispReg32(Registers::D0, m_prevRegs, regs) << " " << DispReg32(Registers::A0, m_prevRegs, regs) << " " << FindSymbol(regs.m_value[Registers::A0] & 0xffffff) << "<br>";
    ref << DispReg32(Registers::D1, m_prevRegs, regs) << " " << DispReg32(Registers::A1, m_prevRegs, regs) << " " << FindSymbol(regs.m_value[Registers::A1] & 0xffffff) << "<br>";
    ref << DispReg32(Registers::D2, m_prevRegs, regs) << " " << DispReg32(Registers::A2, m_prevRegs, regs) << " " << FindSymbol(regs.m_value[Registers::A2] & 0xffffff) << "<br>";
    ref << DispReg32(Registers::D3, m_prevRegs, regs) << " " << DispReg32(Registers::A3, m_prevRegs, regs) << " " << FindSymbol(regs.m_value[Registers::A3] & 0xffffff) << "<br>";
    ref << DispReg32(Registers::D4, m_prevRegs, regs) << " " << DispReg32(Registers::A4, m_prevRegs, regs) << " " << FindSymbol(regs.m_value[Registers::A4] & 0xffffff) << "<br>";
    ref << DispReg32(Registers::D5, m_prevRegs, regs) << " " << DispReg32(Registers::A5, m_prevRegs, regs) << " " << FindSymbol(regs.m_value[Registers::A5] & 0xffffff) << "<br>";
    ref << DispReg32(Registers::D6, m_prevRegs, regs) << " " << DispReg32(Registers::A6, m_prevRegs, regs) << " " << FindSymbol(regs.m_value[Registers::A6] & 0xffffff) << "<br>";
    ref << DispReg32(Registers::D7, m_prevRegs, regs) << " " << DispReg32(Registers::A7, m_prevRegs, regs) << " " << FindSymbol(regs.m_value[Registers::A7] & 0xffffff) << "<br>";
    ref << "<br>";
    ref << QString::asprintf("VBL: %10u Frame Cycles: %6u", GET_REG(regs, VBL), GET_REG(regs, FrameCycles)) << "<br>";
    ref << QString::asprintf("HBL: %10u Line Cycles:  %6u", GET_REG(regs, HBL), GET_REG(regs, LineCycles)) << "<br>";
    ref << "</pre>";
    m_pRegistersTextEdit->setHtml(regsText);
}

QString MainWindow::FindSymbol(uint32_t addr)
{
    Symbol sym;
    if (!m_pTargetModel->GetSymbolTable().FindLowerOrEqual(addr & 0xffffff, sym))
        return QString();

    uint32_t offset = addr - sym.address;
    if (offset)
        return QString::asprintf("%s+%d", sym.name.c_str(), offset);
    return QString::fromStdString(sym.name);
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
    disasmWindowAct0->setChecked(m_pDisasmWidget0->isVisible());
    disasmWindowAct1->setChecked(m_pDisasmWidget1->isVisible());
    memoryWindowAct0->setChecked(m_pMemoryViewWidget0->isVisible());
    memoryWindowAct1->setChecked(m_pMemoryViewWidget1->isVisible());
    graphicsInspectorAct->setChecked(m_pGraphicsInspector->isVisible());
    breakpointsWindowAct->setChecked(m_pBreakpointsWidget->isVisible());
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
    connectAct->setEnabled(!isConnected);
    disconnectAct->setEnabled(isConnected);

    exceptionsAct->setEnabled(isConnected);
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
        m_pDisasmWidget0->setVisible(true);
        m_pDisasmWidget1->setVisible(false);
        m_pMemoryViewWidget0->setVisible(true);
        m_pMemoryViewWidget1->setVisible(false);
        m_pGraphicsInspector->setVisible(true);
        m_pBreakpointsWidget->setVisible(true);
    }
    else
    {

        QDockWidget* wlist[] =
        {
            m_pDisasmWidget0, m_pDisasmWidget1,
            m_pMemoryViewWidget0, m_pMemoryViewWidget1,
            m_pBreakpointsWidget, m_pGraphicsInspector,
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

    m_pDisasmWidget0->saveSettings();
    m_pDisasmWidget1->saveSettings();
    m_pMemoryViewWidget0->saveSettings();
    m_pMemoryViewWidget1->saveSettings();
    m_pGraphicsInspector->saveSettings();
}

void MainWindow::menuConnect()
{
    Connect();
}

void MainWindow::menuDisconnect()
{
    Disconnect();
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("hrdb"),
            tr("hrdb - Hatari remote debugger GUI"));
}

void MainWindow::aboutQt()
{
}

//! [4]
void MainWindow::createActions()
{
    // "File"
    runAct = new QAction(tr("&Run..."), this);
    runAct->setStatusTip(tr("Run Hatari"));
    connect(runAct, &QAction::triggered, this, &MainWindow::Run);

    connectAct = new QAction(tr("&Connect"), this);
    connectAct->setStatusTip(tr("Connect to Hatari"));
    connect(connectAct, &QAction::triggered, this, &MainWindow::Connect);

    disconnectAct = new QAction(tr("&Disconnect"), this);
    disconnectAct->setStatusTip(tr("Disconnect from Hatari"));
    connect(disconnectAct, &QAction::triggered, this, &MainWindow::Disconnect);

    exitAct = new QAction(tr("E&xit"), this);
    exitAct->setShortcuts(QKeySequence::Quit);
    exitAct->setStatusTip(tr("Exit the application"));
    connect(exitAct, &QAction::triggered, this, &QWidget::close);

    // Edit
    exceptionsAct = new QAction(tr("&Exceptions..."), this);
    exceptionsAct->setStatusTip(tr("Disconnect from Hatari"));
    connect(exceptionsAct, &QAction::triggered, this, &MainWindow::ExceptionsDialog);

    // "Window"
    disasmWindowAct0 = new QAction(tr("Disassembly 1"), this);
    disasmWindowAct0->setShortcut(QKeySequence("Alt+D"));
    disasmWindowAct0->setStatusTip(tr("Show the memory window"));
    disasmWindowAct0->setCheckable(true);

    disasmWindowAct1 = new QAction(tr("&Disassembly 2"), this);
    disasmWindowAct1->setStatusTip(tr("Show the memory window"));
    disasmWindowAct1->setCheckable(true);

    memoryWindowAct0 = new QAction(tr("&Memory 1"), this);
    memoryWindowAct0->setShortcut(QKeySequence("Alt+M"));
    memoryWindowAct0->setStatusTip(tr("Show the memory window"));
    memoryWindowAct0->setCheckable(true);

    memoryWindowAct1 = new QAction(tr("Memory 2"), this);
    memoryWindowAct1->setStatusTip(tr("Show the memory window"));
    memoryWindowAct1->setCheckable(true);

    graphicsInspectorAct = new QAction(tr("&Graphics Inspector"), this);
    graphicsInspectorAct->setShortcut(QKeySequence("Alt+G"));
    graphicsInspectorAct->setStatusTip(tr("Show the Graphics Inspector"));
    graphicsInspectorAct->setCheckable(true);

    breakpointsWindowAct = new QAction(tr("&Breakpoints"), this);
    breakpointsWindowAct->setShortcut(QKeySequence("Alt+B"));
    breakpointsWindowAct->setStatusTip(tr("Show the Breakpoints window"));
    breakpointsWindowAct->setCheckable(true);

    connect(disasmWindowAct0, &QAction::triggered, this,     [=] () { this->enableVis(m_pDisasmWidget0); m_pDisasmWidget0->keyFocus(); } );
    connect(disasmWindowAct1, &QAction::triggered, this,     [=] () { this->enableVis(m_pDisasmWidget1); m_pDisasmWidget1->keyFocus(); } );
    connect(memoryWindowAct0, &QAction::triggered, this,     [=] () { this->enableVis(m_pMemoryViewWidget0); m_pMemoryViewWidget0->keyFocus(); } );
    connect(memoryWindowAct1, &QAction::triggered, this,     [=] () { this->enableVis(m_pMemoryViewWidget1); m_pMemoryViewWidget1->keyFocus(); } );
    connect(graphicsInspectorAct, &QAction::triggered, this, [=] () { this->enableVis(m_pGraphicsInspector); m_pGraphicsInspector->keyFocus(); } );
    connect(breakpointsWindowAct, &QAction::triggered, this, [=] () { this->enableVis(m_pBreakpointsWidget); m_pBreakpointsWidget->keyFocus(); } );

    // This should be an action
    new QShortcut(QKeySequence(tr("Shift+Alt+B",  "Add Breakpoint...")),  this, SLOT(addBreakpointPressed()));

    // "About"
    aboutAct = new QAction(tr("&About"), this);
    aboutAct->setStatusTip(tr("Show the application's About box"));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::about);

    aboutQtAct = new QAction(tr("About &Qt"), this);
    aboutQtAct->setStatusTip(tr("Show the Qt library's About box"));
    connect(aboutQtAct, &QAction::triggered, qApp, &QApplication::aboutQt);
    connect(aboutQtAct, &QAction::triggered, this, &MainWindow::aboutQt);
}
//! [7]

//! [8]
void MainWindow::createMenus()
{
    // "File"
    fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(runAct);
    fileMenu->addAction(connectAct);
    fileMenu->addAction(disconnectAct);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAct);

    editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addSeparator();
    editMenu->addAction(exceptionsAct);
    editMenu->addSeparator();

    windowMenu = menuBar()->addMenu(tr("&Window"));
    windowMenu->addAction(disasmWindowAct0);
    windowMenu->addAction(disasmWindowAct1);
    windowMenu->addAction(memoryWindowAct0);
    windowMenu->addAction(memoryWindowAct1);
    windowMenu->addAction(graphicsInspectorAct);
    windowMenu->addAction(breakpointsWindowAct);

    helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(aboutAct);
    helpMenu->addAction(aboutQtAct);
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
