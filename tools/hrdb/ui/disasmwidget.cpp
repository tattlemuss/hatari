#include "disasmwidget.h"

#include <iostream>
#include <QGroupBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QShortcut>
#include <QFontDatabase>
#include <QMenu>
#include <QContextMenuEvent>
#include <QCheckBox>
#include <QCompleter>
#include <QPainter>
#include <QSettings>

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../models/stringparsers.h"
#include "../models/symboltablemodel.h"
#include "../models/memory.h"



DisasmWidget2::DisasmWidget2(QWidget *parent, TargetModel *pTargetModel, Dispatcher* pDispatcher, int windowIndex):
    QWidget(parent),
    m_memory(0, 0),
    m_rowCount(25),
    m_requestedAddress(0),
    m_logicalAddr(0),
    m_requestId(0),
    m_bFollowPC(true),
    m_windowIndex(windowIndex),
    m_pTargetModel(pTargetModel),
    m_pDispatcher(pDispatcher),
    m_bShowHex(true),
    m_rightClickMenu(this),
    m_rightClickRow(-1),
    m_rightClickInstructionAddr(0),
    m_cursorRow(0),
    m_mouseRow(-1)
{
    RecalcColums();
    GetLineHeight();

    SetRowCount(8);
    setMinimumSize(0, 10 * m_lineHeight);

    m_memSlot = static_cast<MemorySlot>(windowIndex + MemorySlot::kDisasm0);

    m_breakpointPixmap   = QPixmap(":/images/breakpoint10.png");
    m_breakpointPcPixmap = QPixmap(":/images/pcbreakpoint10.png");
    m_pcPixmap           = QPixmap(":/images/pc10.png");

    // Actions for right-click menu
    m_pRunUntilAction = new QAction(tr("Run to here"), this);
    m_pBreakpointAction = new QAction(tr("Toggle Breakpoint"), this);
    m_pNopAction = new QAction(tr("Replace with NOPs"), this);

    m_pMemViewAddress[0] = new QAction("", this);
    m_pMemViewAddress[1] = new QAction("", this);
    m_pMemViewAddress[2] = new QAction(tr("Show instruction memory"), this);
    m_pDisassembleAddress[0] = new QAction("", this);
    m_pDisassembleAddress[1] = new QAction("", this);

    QMenu* pViewMenu = new QMenu("View", this);
    pViewMenu->addAction(m_pMemViewAddress[2]);
    pViewMenu->addAction(m_pMemViewAddress[0]);
    pViewMenu->addAction(m_pMemViewAddress[1]);
    pViewMenu->addAction(m_pDisassembleAddress[0]);
    pViewMenu->addAction(m_pDisassembleAddress[1]);

    QMenu* pEditMenu = new QMenu("Edit", this);
    pEditMenu->addAction(m_pNopAction); //

    m_rightClickMenu.addAction(m_pRunUntilAction);
    m_rightClickMenu.addAction(m_pBreakpointAction);
    m_rightClickMenu.addMenu(pEditMenu);
    m_rightClickMenu.addMenu(pViewMenu);

    new QShortcut(QKeySequence(tr("F3",     "Run to cursor")),        this, SLOT(runToCursor()));
    new QShortcut(QKeySequence(tr("Ctrl+B", "Toggle breakpoint")),    this, SLOT(toggleBreakpoint()));

    // Target connects
    connect(m_pTargetModel, &TargetModel::startStopChangedSignal, this, &DisasmWidget2::startStopChangedSlot);
    connect(m_pTargetModel, &TargetModel::memoryChangedSignal, this, &DisasmWidget2::memoryChangedSlot);
    connect(m_pTargetModel, &TargetModel::breakpointsChangedSignal, this, &DisasmWidget2::breakpointsChangedSlot);
    connect(m_pTargetModel, &TargetModel::symbolTableChangedSignal, this, &DisasmWidget2::symbolTableChangedSlot);
    connect(m_pTargetModel, &TargetModel::connectChangedSignal, this, &DisasmWidget2::connectChangedSlot);
    connect(m_pTargetModel, &TargetModel::registersChangedSignal, this, &DisasmWidget2::CalcOpAddresses);
    connect(m_pTargetModel, &TargetModel::otherMemoryChanged,       this, &DisasmWidget2::otherMemoryChangedSlot);

    // UI connects
    connect(m_pRunUntilAction,       &QAction::triggered,                  this, &DisasmWidget2::runToCursorRightClick);
    connect(m_pBreakpointAction,     &QAction::triggered,                  this, &DisasmWidget2::toggleBreakpointRightClick);
    connect(m_pNopAction,            &QAction::triggered,                  this, &DisasmWidget2::nopRightClick);
    connect(m_pMemViewAddress[0],    &QAction::triggered,                  this, &DisasmWidget2::memoryViewAddr0);
    connect(m_pMemViewAddress[1],    &QAction::triggered,                  this, &DisasmWidget2::memoryViewAddr1);
    connect(m_pMemViewAddress[2],    &QAction::triggered,                  this, &DisasmWidget2::memoryViewAddrInst);
    connect(m_pDisassembleAddress[0],&QAction::triggered,                  this, &DisasmWidget2::disasmViewAddr0);
    connect(m_pDisassembleAddress[1],&QAction::triggered,                  this, &DisasmWidget2::disasmViewAddr1);
    setMouseTracking(true);

    this->setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    repaint();
}

DisasmWidget2::~DisasmWidget2()
{

}

void DisasmWidget2::SetAddress(uint32_t addr)
{
    // Request memory for this region and save the address.
    m_logicalAddr = addr;
    RequestMemory();
    emit addressChanged(m_logicalAddr);
}

// Request enough memory based on m_rowCount and m_logicalAddr
void DisasmWidget2::RequestMemory()
{
    uint32_t addr = m_logicalAddr;
    uint32_t lowAddr = (addr > 100) ? addr - 100 : 0;
    uint32_t size = ((m_rowCount * 10) + 100);
    if (m_pTargetModel->IsConnected())
    {
        m_requestId = m_pDispatcher->RequestMemory(m_memSlot, lowAddr, size);
    }
}

bool DisasmWidget2::GetEA(uint32_t row, int operandIndex, uint32_t &addr)
{
    if (row >= m_opAddresses.size())
        return false;

    if (operandIndex >= 2)
        return false;

    addr = m_opAddresses[row].address[operandIndex];
    return m_opAddresses[row].valid[operandIndex];
}

bool DisasmWidget2::GetInstructionAddr(int row, uint32_t &addr) const
{
    if (row >= m_disasm.lines.size())
        return false;
    addr = m_disasm.lines[row].address;
    return true;
}

bool DisasmWidget2::SetAddress(std::string addrStr)
{
    uint32_t addr;
    if (!StringParsers::ParseExpression(addrStr.c_str(), addr,
                                       m_pTargetModel->GetSymbolTable(), m_pTargetModel->GetRegs()))
    {
        return false;
    }

    SetAddress(addr);
    return true;
}

void DisasmWidget2::MoveUp()
{
    if (m_cursorRow != 0)
    {
        --m_cursorRow;
        update();
        return;
    }

    if (m_requestId != 0)
        return; // not up to date

    // Disassemble upwards to see if something sensible appears.
    // Stop at the first valid instruction opcode.
    // Max instruction size is 10 bytes.
    if (m_requestId == 0)
    {
        for (uint32_t off = 2; off <= 10; off += 2)
        {
            uint32_t targetAddr = m_logicalAddr - off;
            // Check valid memory
            if (m_memory.GetAddress() > targetAddr ||
                m_memory.GetAddress() + m_memory.GetSize() <= targetAddr)
            {
                continue;
            }
            // Get memory buffer for this range
            uint32_t offset = targetAddr - m_memory.GetAddress();
            uint32_t size = m_memory.GetSize() - offset;
            buffer_reader disasmBuf(m_memory.GetData() + offset, size);
            instruction inst;
            if (Disassembler::decode_inst(disasmBuf, inst) == 0)
            {
                if (inst.opcode != Opcode::NONE)
                {
                    SetAddress(targetAddr);
                    return;
                }
            }
        }
    }

    if (m_logicalAddr > 2)
        SetAddress(m_logicalAddr - 2);
    else
        SetAddress(0);
}

void DisasmWidget2::MoveDown()
{
    if (m_requestId != 0)
        return; // not up to date

    if (m_cursorRow < m_rowCount -1)
    {
        ++m_cursorRow;
        update();
        return;
    }

    if (m_disasm.lines.size() > 0)
    {
        // Find our current line in disassembly
        for (size_t i = 0; i < m_disasm.lines.size(); ++i)
        {
            if (m_disasm.lines[i].address == m_logicalAddr)
            {
                // This will go off and request the memory itself
                SetAddress(m_disasm.lines[i].GetEnd());
                return;
            }
        }
        // Default to line 1
        SetAddress(m_disasm.lines[0].GetEnd());
    }
}

void DisasmWidget2::PageUp()
{
    if (m_requestId != 0)
        return; // not up to date

    // TODO we should actually disassemble upwards to see if something sensible appears
    if (m_logicalAddr > 20)
        SetAddress(m_logicalAddr - 20);
    else
        SetAddress(0);
}

void DisasmWidget2::PageDown()
{
    if (m_requestId != 0)
        return; // not up to date

    if (m_disasm.lines.size() > 0)
    {
        // This will go off and request the memory itself
        SetAddress(m_disasm.lines.back().GetEnd());
    }
}

void DisasmWidget2::RunToRow(int row)
{
    if (row >= 0 && row < m_disasm.lines.size())
    {
        Disassembler::line& line = m_disasm.lines[row];
        m_pDispatcher->RunToPC(line.address);
    }
}

void DisasmWidget2::startStopChangedSlot()
{
    // Request new memory for the view
    if (!m_pTargetModel->IsRunning())
    {
        // Decide what to request.
        if (m_bFollowPC)
        {
            // Update to PC position
            SetAddress(m_pTargetModel->GetPC());
        }
        else
        {
            // Just request what we had already.
            RequestMemory();
        }
    }
}

void DisasmWidget2::connectChangedSlot()
{
    if (!m_pTargetModel->IsConnected())
    {
        m_disasm.lines.clear();
        m_rowCount = 1;
        m_memory.Clear();
    }
}

void DisasmWidget2::memoryChangedSlot(int memorySlot, uint64_t commandId)
{
    if (memorySlot != m_memSlot)
        return;

    // Only update for the last request we added
    if (commandId != m_requestId)
        return;

    printf("Mem changed current\n");
    const Memory* pMemOrig = m_pTargetModel->GetMemory(m_memSlot);
    if (!pMemOrig)
        return;

    printf("Mem changed %x\n", pMemOrig->GetAddress());
    if (m_logicalAddr == kInvalid)
    {
        m_logicalAddr = pMemOrig->GetAddress();
        emit addressChanged(m_logicalAddr);
    }

    // Cache it for later
    m_memory = *pMemOrig;
    CalcDisasm();

    // Clear the request, to say we are up to date
    m_requestId = 0;
    update();
}

void DisasmWidget2::breakpointsChangedSlot(uint64_t /*commandId*/)
{
    // Cache data
    m_breakpoints = m_pTargetModel->GetBreakpoints();
    CalcDisasm();
    update();
}

void DisasmWidget2::symbolTableChangedSlot(uint64_t /*commandId*/)
{
    // Don't copy here, just force a re-read
//    emit dataChanged(this->createIndex(0, 0), this->createIndex(m_rowCount - 1, kColCount));
    CalcDisasm();
    update();
}

void DisasmWidget2::otherMemoryChangedSlot(uint32_t address, uint32_t size)
{
    // Do a re-request if our memory is touched
    uint32_t ourAddr = m_logicalAddr;
    uint32_t ourSize = ((m_rowCount * 10) + 100);
    if (Overlaps(ourAddr, ourSize, address, size))
        RequestMemory();
}

void DisasmWidget2::paintEvent(QPaintEvent* ev)
{
    QWidget::paintEvent(ev);

    // CAREFUL! This could lead to an infinite loop of redraws if we are not.
    RecalcRowCount();

    QPainter painter(this);
    const QPalette& pal = this->palette();

    if (hasFocus())
    {
        painter.setPen(QPen(pal.dark(), 6));
        painter.drawRect(this->rect());
    }

    painter.setFont(monoFont);
    QFontMetrics info(painter.fontMetrics());

    // Anything to show?
    if (m_disasm.lines.size() == 0)
        return;

    int char_width = info.horizontalAdvance("0");
    int y_base = info.ascent();

    {
        if (m_mouseRow != -1)
        {
            painter.setPen(Qt::PenStyle::DashLine);
            painter.setBrush(Qt::BrushStyle::NoBrush);
            painter.drawRect(0, m_mouseRow * m_lineHeight, rect().width(), m_lineHeight);
        }

        int y_curs = m_cursorRow * m_lineHeight;       // compensate for descenders TODO use ascent()
        painter.setPen(Qt::PenStyle::NoPen);
        painter.setBrush(pal.highlight());
        painter.drawRect(0, y_curs, rect().width(), m_lineHeight);
    }

    for (int row = 0; row < m_rowTexts.size(); ++row)
    {
        if (row == m_cursorRow)
            painter.setPen(pal.highlightedText().color());
        else
            painter.setPen(pal.text().color());

        int y = y_base + row * m_lineHeight;
        const RowText& t = m_rowTexts[row];

        painter.drawText(m_symbolCol * char_width, y, t.symbol);
        painter.drawText(m_addressCol * char_width, y, t.address);
        if (m_bShowHex)
            painter.drawText(m_hexCol * char_width, y, t.hex);
        painter.drawText(m_disasmCol * char_width, y, t.disasm);
        painter.drawText(m_commentsCol * char_width, y, t.comments);

        if (t.isPc)
        {
            /*
            painter.drawPixmap(pcCol * char_width,
                               row * m_lineHeight + (m_lineHeight - m_pcPixmap.height()) / 2,
                               m_pcPixmap);*/
            painter.drawText(m_pcCol * char_width, y, ">");
        }

        if (t.isBreakpoint)
            painter.drawText(m_bpCol * char_width, y, "*");
    }
}

void DisasmWidget2::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
    case Qt::Key_Up:         MoveUp();            return;
    case Qt::Key_Down:       MoveDown();          return;
    case Qt::Key_PageUp:     PageUp();            return;
    case Qt::Key_PageDown:   PageDown();          return;
    default: break;
    }
    QWidget::keyPressEvent(event);
}

void DisasmWidget2::mouseMoveEvent(QMouseEvent *event)
{
    m_mouseRow = int(event->localPos().y() / m_lineHeight);
    if (this->underMouse())
        update();       // redraw highlight

    QWidget::mouseMoveEvent(event);
}

bool DisasmWidget2::event(QEvent* ev)
{
    if (ev->type() == QEvent::Leave)
    {
        // overwrite handling of PolishRequest if any
        m_mouseRow = -1;
        update();
    }
    return QWidget::event(ev);
}

void DisasmWidget2::CalcDisasm()
{
    // Make sure the data we get back matches our expectations...
    if (m_logicalAddr < m_memory.GetAddress())
        return;
    if (m_logicalAddr >= m_memory.GetAddress() + m_memory.GetSize())
        return;

    // Work out where we need to start disassembling from
    uint32_t offset = m_logicalAddr - m_memory.GetAddress();
    uint32_t size = m_memory.GetSize() - offset;
    buffer_reader disasmBuf(m_memory.GetData() + offset, size);
    m_disasm.lines.clear();
    Disassembler::decode_buf(disasmBuf, m_disasm, m_logicalAddr, m_rowCount);
    CalcOpAddresses();

    // Recalc Text (which depends on e.g. symbols
    m_rowTexts.clear();
    for (size_t row = 0; row < m_disasm.lines.size(); ++row)
    {
        RowText t;
        Disassembler::line& line = m_disasm.lines[row];

        // Address
        uint32_t addr = line.address;
        t.address = QString::asprintf("%08x", addr);

        // Symbol
        QString addrText;
        Symbol sym;
        if (row == 0)
        {
            // show symbol + offset if necessary for the top line
            if (m_pTargetModel->GetSymbolTable().FindLowerOrEqual(addr, sym))
            {
                if (addr == sym.address)
                    t.symbol = QString::fromStdString(sym.name) + ":";
                else {
                    t.symbol = QString::asprintf("%s+$%x:", sym.name.c_str(), addr - sym.address);
                }
            }
        }
        else {
            if (m_pTargetModel->GetSymbolTable().Find(addr, sym))
                t.symbol = QString::fromStdString(sym.name) + ":";
        }

        // Hex
        for (uint32_t i = 0; i < line.inst.byte_count; ++i)
            t.hex += QString::asprintf("%02x", line.mem[i]);

        // Disassembly
        QTextStream ref(&t.disasm);
        Disassembler::print(line.inst, line.address, ref);

        // Comments
        QString str;
        QTextStream refC(&t.comments);
        Registers regs = m_pTargetModel->GetRegs();
        printEA(line.inst.op0, regs, line.address, refC);
        if (str.size() != 0)
            ref << "  ";
        printEA(line.inst.op1, regs, line.address, refC);

        // Breakpoint/PC
        t.isPc = line.address == m_pTargetModel->GetPC();
        t.isBreakpoint = false;
        for (size_t i = 0; i < m_breakpoints.m_breakpoints.size(); ++i)
        {
            if (m_breakpoints.m_breakpoints[i].m_pcHack == line.address)
            {
                t.isBreakpoint = true;
                break;
            }
        }

        m_rowTexts.push_back(t);
    }
}

void DisasmWidget2::CalcOpAddresses()
{
    // Precalc EAs
    m_opAddresses.clear();
    m_opAddresses.resize(m_disasm.lines.size());
    const Registers& regs = m_pTargetModel->GetRegs();
    for (size_t i = 0; i < m_disasm.lines.size(); ++i)
    {
        const instruction& inst = m_disasm.lines[i].inst;
        OpAddresses& addrs = m_opAddresses[i];
        bool useRegs = (i == 0);
        addrs.valid[0] = Disassembler::calc_fixed_ea(inst.op0, useRegs, regs, m_disasm.lines[i].address, addrs.address[0]);
        addrs.valid[1] = Disassembler::calc_fixed_ea(inst.op1, useRegs, regs, m_disasm.lines[i].address, addrs.address[1]);
    }
}

void DisasmWidget2::ToggleBreakpoint(int row)
{
    // set a breakpoint
    if (row < 0 || row >= m_disasm.lines.size())
        return;

    Disassembler::line& line = m_disasm.lines[row];
    uint32_t addr = line.address;
    bool removed = false;

    const Breakpoints& bp = m_pTargetModel->GetBreakpoints();
    for (size_t i = 0; i < bp.m_breakpoints.size(); ++i)
    {
        if (bp.m_breakpoints[i].m_pcHack == addr)
        {
            m_pDispatcher->DeleteBreakpoint(bp.m_breakpoints[i].m_id);
            removed = true;
        }
    }
    if (!removed)
    {
        QString cmd = QString::asprintf("pc = $%x", addr);
        m_pDispatcher->SetBreakpoint(cmd.toStdString().c_str(), false);
    }
}

void DisasmWidget2::NopRow(int row)
{
    if (row >= m_disasm.lines.size())
        return;

    Disassembler::line& line = m_disasm.lines[row];
    uint32_t addr = line.address;

    QString command = QString::asprintf("memset %u %u ", addr, line.inst.byte_count);
    for (int i = 0; i <  line.inst.byte_count /2; ++i)
        command += "4e71";

    m_pDispatcher->SendCommandPacket(command.toStdString().c_str());
}

void DisasmWidget2::SetRowCount(int count)
{
    if (count != m_rowCount)
    {
//        emit beginResetModel();
        m_rowCount = count;

        // Do we need more data?
        CalcDisasm();
        if (m_disasm.lines.size() < m_rowCount)
        {
            // We need more memory
            RequestMemory();
        }

//        emit endResetModel();
    }
}

void DisasmWidget2::SetShowHex(bool show)
{
    m_bShowHex = show;
    RecalcColums();
    update();
}

void DisasmWidget2::SetFollowPC(bool bFollow)
{
    m_bFollowPC = bFollow;
    update();
}


void DisasmWidget2::printEA(const operand& op, const Registers& regs, uint32_t address, QTextStream& ref) const
{
    uint32_t ea;

    // Only do a full analysis if this is the PC and therefore the registers are valid...
    if (Disassembler::calc_fixed_ea(op, address == m_pTargetModel->GetPC(), regs, address, ea))
    {
        ea &= 0xffffff; // mask for ST hardware range
        ref << "$" << QString::asprintf("%x", ea);
        Symbol sym;
        if (m_pTargetModel->GetSymbolTable().FindLowerOrEqual(ea, sym))
        {
            uint32_t offset = ea - sym.address;
            if (offset)
                ref << QString::asprintf(" %s+$%x", sym.name.c_str(), offset);
            else
                ref << " " << QString::fromStdString(sym.name);
        }
    };
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void DisasmWidget2::contextMenuEvent(QContextMenuEvent *event)
{
    m_rightClickRow = event->y() / m_lineHeight;
    if (m_rightClickRow < 0 || m_rightClickRow >= m_rowTexts.size())
        return;

    m_rightClickInstructionAddr = 0;
    bool vis = GetInstructionAddr(m_rightClickRow, m_rightClickInstructionAddr);
    m_pMemViewAddress[2]->setVisible(vis);
    m_pMemViewAddress[2]->setText(QString::asprintf("Show Instruction Memory ($%x)", m_rightClickInstructionAddr));

    // Set up relevant menu items
    for (uint32_t op = 0; op < 2; ++op)
    {
        if (GetEA(m_rightClickRow, op, m_rightClickAddr[op]))
        {
            m_pMemViewAddress[op]->setText(QString::asprintf("Show Memory at $%x", m_rightClickAddr[op]));
            m_pMemViewAddress[op]->setVisible(true);
            m_pDisassembleAddress[op]->setText(QString::asprintf("Disassemble at $%x", m_rightClickAddr[op]));
            m_pDisassembleAddress[op]->setVisible(true);
        }
        else
        {
            m_pMemViewAddress[op]->setVisible(false);
            m_pDisassembleAddress[op]->setVisible(false);
        }
    }
    // Run it
    m_rightClickMenu.exec(event->globalPos());
}

void DisasmWidget2::runToCursorRightClick()
{
    RunToRow(m_rightClickRow);
    m_rightClickRow = -1;
}

void DisasmWidget2::toggleBreakpointRightClick()
{
    ToggleBreakpoint(m_rightClickRow);
    m_rightClickRow = -1;
}

void DisasmWidget2::nopRightClick()
{
    NopRow(m_rightClickRow);
    m_rightClickRow = -1;
}

void DisasmWidget2::memoryViewAddrInst()
{
    emit m_pTargetModel->addressRequested(1, true, m_rightClickInstructionAddr);
}

void DisasmWidget2::memoryViewAddr0()
{
    emit m_pTargetModel->addressRequested(1, true, m_rightClickAddr[0]);
}

void DisasmWidget2::memoryViewAddr1()
{
    emit m_pTargetModel->addressRequested(1, true, m_rightClickAddr[1]);
}

void DisasmWidget2::disasmViewAddr0()
{
    emit m_pTargetModel->addressRequested(1, false, m_rightClickAddr[0]);
}

void DisasmWidget2::disasmViewAddr1()
{
    emit m_pTargetModel->addressRequested(1, false, m_rightClickAddr[1]);
}

void DisasmWidget2::runToCursor()
{
    if (m_cursorRow != -1)
        RunToRow(m_cursorRow);
}

void DisasmWidget2::toggleBreakpoint()
{
    if (m_cursorRow != -1)
        ToggleBreakpoint(m_cursorRow);
}

void DisasmWidget2::resizeEvent(QResizeEvent* )
{
    RecalcRowCount();
}

void DisasmWidget2::RecalcRowCount()
{
    int h = this->rect().height();
    int rowh = m_lineHeight;
    if (rowh != 0)
        SetRowCount(h / rowh);
}

void DisasmWidget2::GetLineHeight()
{
    monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QFontMetrics info(monoFont);
    m_lineHeight = info.lineSpacing();
}

void DisasmWidget2::RecalcColums()
{
    m_symbolCol = 1;
    m_addressCol = m_symbolCol + 19;
    m_pcCol = m_addressCol + 9;
    m_bpCol = m_pcCol + 1;
    m_hexCol = m_bpCol + 2;
    m_disasmCol = m_bShowHex ? m_hexCol + 10 * 2 + 1 : m_hexCol; // max size 10 bytes (opcode + 2 longs)
    m_commentsCol = m_disasmCol + 40;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

DisasmViewWidget::DisasmViewWidget(QWidget *parent, TargetModel* pTargetModel, Dispatcher* pDispatcher, int windowIndex) :
    QDockWidget(parent),
    m_pTargetModel(pTargetModel),
    m_pDispatcher(pDispatcher),
    m_windowIndex(windowIndex)
{
    QString key = QString::asprintf("DisasmView%d", m_windowIndex);
    setObjectName(key);

    m_pDisasmWidget = new DisasmWidget2(this, pTargetModel, pDispatcher, windowIndex);
    m_pDisasmWidget->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));

    // Top group box
    m_pLineEdit = new QLineEdit(this);
    m_pShowHex = new QCheckBox("Show hex", this);
    m_pShowHex->setTristate(false);
    m_pShowHex->setChecked(m_pDisasmWidget->GetShowHex());
    m_pFollowPC = new QCheckBox("Follow PC", this);
    m_pFollowPC->setTristate(false);
    m_pFollowPC->setChecked(m_pDisasmWidget->GetFollowPC());

    const QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    //m_pTableView->setFont(monoFont);
    QFontMetrics fm(monoFont);

    // Layouts
    QVBoxLayout* pMainLayout = new QVBoxLayout;
    QHBoxLayout* pTopLayout = new QHBoxLayout;
    auto pMainRegion = new QWidget(this);   // whole panel
    auto pTopRegion = new QWidget(this);    // top buttons/edits
    //pMainGroupBox->setFlat(true);

    pTopLayout->addWidget(m_pLineEdit);
    pTopLayout->addWidget(m_pShowHex);
    pTopLayout->addWidget(m_pFollowPC);

    pMainLayout->addWidget(pTopRegion);
    pMainLayout->addWidget(m_pDisasmWidget);
    pMainLayout->setAlignment(Qt::Alignment(Qt::AlignTop));

    pTopRegion->setLayout(pTopLayout);
    pMainRegion->setLayout(pMainLayout);
    setWidget(pMainRegion);

    m_pSymbolTableModel = new SymbolTableModel(this, m_pTargetModel->GetSymbolTable());
    QCompleter* pCompl = new QCompleter(m_pSymbolTableModel, this);
    pCompl->setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);
    m_pLineEdit->setCompleter(pCompl);


    // Now that everything is set up we can load the setings
    loadSettings();

    // Listen for start/stop, so we can update our memory request
    connect(m_pDisasmWidget,      &DisasmWidget2::addressChanged,       this, &DisasmViewWidget::UpdateTextBox);
    connect(m_pLineEdit,    &QLineEdit::returnPressed,            this, &DisasmViewWidget::returnPressedSlot);
    connect(m_pLineEdit,    &QLineEdit::textEdited,               this, &DisasmViewWidget::textChangedSlot);
    connect(m_pFollowPC,    &QCheckBox::clicked,                  this, &DisasmViewWidget::followPCClickedSlot);
    connect(m_pShowHex,     &QCheckBox::clicked,                  this, &DisasmViewWidget::showHexClickedSlot);

    this->resizeEvent(nullptr);
}

void DisasmViewWidget::requestAddress(int windowIndex, bool isMemory, uint32_t address)
{
    if (isMemory)
        return;

    if (windowIndex != m_windowIndex)
        return;

    m_pDisasmWidget->SetAddress(std::to_string(address));
    m_pDisasmWidget->SetFollowPC(false);
    m_pFollowPC->setChecked(false);
    setVisible(true);
}

void DisasmViewWidget::keyFocus()
{
    activateWindow();
    m_pDisasmWidget->setFocus();
}

void DisasmViewWidget::loadSettings()
{
    QSettings settings;
    QString key = QString::asprintf("DisasmView%d", m_windowIndex);
    settings.beginGroup(key);

    //restoreGeometry(settings.value("geometry").toByteArray());
    m_pDisasmWidget->SetShowHex(settings.value("showHex", QVariant(true)).toBool());
    m_pDisasmWidget->SetFollowPC(settings.value("followPC", QVariant(true)).toBool());
    m_pShowHex->setChecked(m_pDisasmWidget->GetShowHex());
    m_pFollowPC->setChecked(m_pDisasmWidget->GetFollowPC());
    settings.endGroup();
}

void DisasmViewWidget::saveSettings()
{
    QSettings settings;
    QString key = QString::asprintf("DisasmView%d", m_windowIndex);
    settings.beginGroup(key);

    //settings.setValue("geometry", saveGeometry());
    settings.setValue("showHex", m_pDisasmWidget->GetShowHex());
    settings.setValue("followPC", m_pDisasmWidget->GetFollowPC());
    settings.endGroup();
}

void DisasmViewWidget::keyDownPressed()
{
    m_pDisasmWidget->MoveDown();
}
void DisasmViewWidget::keyUpPressed()
{
    m_pDisasmWidget->MoveUp();
}

void DisasmViewWidget::keyPageDownPressed()
{
    m_pDisasmWidget->PageDown();
}

void DisasmViewWidget::keyPageUpPressed()
{
    m_pDisasmWidget->PageUp();
}

void DisasmViewWidget::returnPressedSlot()
{
    QColor col = m_pDisasmWidget->SetAddress(m_pLineEdit->text().toStdString()) ?
                      Qt::white : Qt::red;
    QPalette pal = m_pLineEdit->palette();
    pal.setColor(QPalette::Base, col);
    m_pLineEdit-> setAutoFillBackground(true);
    m_pLineEdit->setPalette(pal);
}

void DisasmViewWidget::textChangedSlot()
{
    uint32_t addr;
    QColor col = Qt::green;
    if (!StringParsers::ParseExpression(m_pLineEdit->text().toStdString().c_str(), addr,
                                       m_pTargetModel->GetSymbolTable(), m_pTargetModel->GetRegs()))
    {
        col = Qt::red;
    }

    QPalette pal = m_pLineEdit->palette();
    pal.setColor(QPalette::Base, col);
    m_pLineEdit-> setAutoFillBackground(true);
    m_pLineEdit->setPalette(pal);
}

void DisasmViewWidget::showHexClickedSlot()
{
    m_pDisasmWidget->SetShowHex(m_pShowHex->isChecked());
}

void DisasmViewWidget::followPCClickedSlot()
{
    m_pDisasmWidget->SetFollowPC(m_pFollowPC->isChecked());
}

void DisasmViewWidget::UpdateTextBox()
{
    uint32_t addr = m_pDisasmWidget->GetAddress();
    m_pLineEdit->setText(QString::asprintf("$%x", addr));
}

