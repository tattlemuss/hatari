#include "disasmwidget.h"

#include <iostream>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QShortcut>
#include <QMenu>
#include <QContextMenuEvent>
#include <QCheckBox>
#include <QCompleter>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QClipboard>

#include "hopper68/buffer68.h"
#include "hopper56/buffer56.h"

#include "../hardware/tos.h"
#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../models/stringparsers.h"
#include "../models/stringformat.h"
#include "../models/symboltablemodel.h"
#include "../models/memory.h"
#include "../models/session.h"
#include "colouring.h"
#include "quicklayout.h"
#include "symboltext.h"
#include "qtversionwrapper.h"

DisasmWidget::DisasmWidget(QWidget *parent, Session* pSession, int windowIndex, QAction* pSearchAction):
    QWidget(parent),
    m_rowCount(25),
    m_proc(kProcCpu),
    m_minInstSize(2U),
    m_maxInstSize(32U),
    m_memory(MEM_CPU, 0, 0),
    m_logicalAddr(0),
    m_requestId(0),
    m_bFollowPC(true),
    m_windowIndex(windowIndex),
    m_pSession(pSession),
    m_pTargetModel(pSession->m_pTargetModel),
    m_pDispatcher(pSession->m_pDispatcher),
    m_pSearchAction(pSearchAction),
    m_bShowHex(true),
    m_rightClickRow(-1),
    m_cursorRow(0),
    m_mouseRow(-1),
    m_wheelAngleDelta(0)
{
    m_lastAddress[kProcCpu] = 0;
    m_lastAddress[kProcDsp] = 0;

    SetProc(kProcCpu);
    UpdateFont();

    SetRowCount(8);
    setMinimumSize(0, 10 * m_lineHeight);
    setAutoFillBackground(true);

    m_memSlot = static_cast<MemorySlot>(windowIndex + MemorySlot::kDisasm0);

    m_breakpointPixmap   = QPixmap(":/images/breakpoint10.png");
    m_breakpointPcPixmap = QPixmap(":/images/pcbreakpoint10.png");
    m_pcPixmap           = QPixmap(":/images/pc10.png");

    // Actions for right-click menu
    m_pRunUntilAction = new QAction(tr("Run to here"), this);
    m_pBreakpointAction = new QAction(tr("Toggle Breakpoint"), this);
    m_pSetPcAction = new QAction(tr("Set PC to here"), this);
    m_pNopAction = new QAction(tr("Replace with NOPs"), this);
    m_pCopyAction = new QAction(tr("Copy"), this);
    m_pCopyAction->setShortcut(QKeySequence::Copy);
    m_pCopyAction->setShortcutContext(Qt::WidgetShortcut);
    this->addAction(m_pCopyAction); // this is needed for keyboard access

    m_pEditMenu = new QMenu("Edit", this);
    m_pEditMenu->addAction(m_pNopAction);

    RecalcColumnWidths();

    // Target connects
    connect(m_pTargetModel, &TargetModel::startStopChangedSignal,   this, &DisasmWidget::startStopChanged);
    connect(m_pTargetModel, &TargetModel::memoryChangedSignal,      this, &DisasmWidget::memoryChanged);
    connect(m_pTargetModel, &TargetModel::breakpointsChangedSignal, this, &DisasmWidget::breakpointsChanged);
    connect(m_pTargetModel, &TargetModel::symbolTableChangedSignal, this, &DisasmWidget::symbolTableChanged);
    connect(m_pTargetModel, &TargetModel::connectChangedSignal,     this, &DisasmWidget::connectChanged);
    connect(m_pTargetModel, &TargetModel::registersChangedSignal,   this, &DisasmWidget::CalcAnnotations68);
    connect(m_pTargetModel, &TargetModel::otherMemoryChangedSignal, this, &DisasmWidget::otherMemoryChanged);
    connect(m_pTargetModel, &TargetModel::profileChangedSignal,     this, &DisasmWidget::profileChanged);
    connect(m_pTargetModel, &TargetModel::mainStateCompletedSignal, this, &DisasmWidget::mainStateCompleted);
    connect(m_pTargetModel, &TargetModel::configChangedSignal,      this, &DisasmWidget::configChanged);

    // UI connects
    connect(m_pRunUntilAction,       &QAction::triggered, this, &DisasmWidget::runToCursorRightClick);
    connect(m_pBreakpointAction,     &QAction::triggered, this, &DisasmWidget::toggleBreakpointRightClick);
    connect(m_pSetPcAction,          &QAction::triggered, this, &DisasmWidget::setPCRightClick);
    connect(m_pNopAction,            &QAction::triggered, this, &DisasmWidget::nopRightClick);
    connect(m_pCopyAction,           &QAction::triggered, this, &DisasmWidget::copyRightClick);

    connect(m_pSession, &Session::settingsChanged, this, &DisasmWidget::settingsChangedSlot);

    setMouseTracking(true);

    this->setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    configChanged();        // Disallow DSP if necessary
}

DisasmWidget::~DisasmWidget()
{

}

bool DisasmWidget::GetAddressAtCursor(uint32_t& addr) const
{
    addr = 0;
    return GetInstructionAddr(m_cursorRow, addr);
}

void DisasmWidget::SetAddress(uint32_t addr)
{
    // Request memory for this region and save the address.
    m_logicalAddr = addr;
    RequestMemory();
    emit addressChanged(m_logicalAddr);
}

// Request enough memory based on m_rowCount and m_logicalAddr
void DisasmWidget::RequestMemory()
{
    uint32_t addr = m_logicalAddr;

    uint32_t pageSize = static_cast<uint32_t>(m_rowCount) * m_maxInstSize;  // Maximum disassem size for the page "above"
    uint32_t lowAddr = (addr > pageSize) ? addr - pageSize : 0;
    uint32_t highAddr = addr + pageSize;

    uint32_t size = highAddr - lowAddr;
    if (m_pTargetModel->IsConnected())
    {
        MemSpace space = (m_proc == kProcCpu) ? MEM_CPU : MEM_P;
        m_requestId = m_pDispatcher->ReadMemory(m_memSlot, space, lowAddr, size);
    }
}

bool DisasmWidget::GetEA(int row, int operandIndex, MemAddr& addr)
{
    if (row >= m_disasm.size())
        return false;

    if (operandIndex >= 2)
        return false;

    addr = m_disasm[row].annotations.address[operandIndex];
    return m_disasm[row].annotations.valid[operandIndex];
}

bool DisasmWidget::GetInstructionAddr(int row, uint32_t &addr) const
{
    if (row >= m_disasm.size())
        return false;
    addr = m_disasm[row].address;
    return true;
}

bool DisasmWidget::SetAddress(std::string addrStr)
{
    // TODO: how do we handle address modes?
    uint32_t addr;
    if (!StringParsers::ParseCpuExpression(addrStr.c_str(), addr,
                                       m_pTargetModel->GetSymbolTable(), m_pTargetModel->GetRegs()))
    {
        return false;
    }

    SetAddress(addr);
    return true;
}

bool DisasmWidget::SetSearchResultAddress(uint32_t addr)
{
    SetAddress(addr);
    m_cursorRow = 0;
    return true;
}

void DisasmWidget::MoveUp()
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
    if (m_requestId == 0)
    {
        for (uint32_t off = m_minInstSize; off <= m_maxInstSize; off += m_minInstSize)
        {
            uint32_t targetAddr = m_logicalAddr - off;

            // TODO: this is rather ugly.
            if (m_proc == kProcCpu)
            {
                // Check valid memory
                if (m_memory.GetAddress() > targetAddr ||
                    m_memory.GetAddress() + m_memory.GetSize() <= targetAddr)
                {
                    continue;
                }
                // Get memory buffer for this range
                uint32_t offset = targetAddr - m_memory.GetAddress();
                uint32_t size = m_memory.GetSize() - offset;

                hop68::buffer_reader disasmBuf(m_memory.GetData() + offset, size, m_memory.GetAddress() + offset);
                hop68::instruction inst;
                Disassembler::decode_inst(disasmBuf, inst, m_pTargetModel->GetDisasmSettings());
                if (inst.opcode != hop68::Opcode::NONE)
                {
                    SetAddress(targetAddr);
                    return;
                }
            }
            else
            {
                // Check valid memory
                if (m_memory.GetAddress() > targetAddr ||
                    m_memory.GetAddress() + m_memory.GetSize() / 3 <= targetAddr)
                {
                    continue;
                }
                // Get memory buffer for this range
                uint32_t offset = targetAddr - m_memory.GetAddress();
                uint32_t size = m_memory.GetSize() - offset * 3;

                hop56::buffer_reader disasmBuf(m_memory.GetData() + offset * 3, size, m_memory.GetAddress() + offset);
                hop56::instruction inst;
                hop56::decode_settings dummy;
                Disassembler56::decode_inst(disasmBuf, inst, dummy);
                if (inst.opcode != hop56::Opcode::INVALID)
                {
                    SetAddress(targetAddr);
                    return;
                }
            }
        }
    }

    if (m_logicalAddr > m_minInstSize)
        SetAddress(m_logicalAddr - m_minInstSize);
    else
        SetAddress(0);
}

void DisasmWidget::MoveDown()
{
    if (m_requestId != 0)
        return; // not up to date

    if (m_cursorRow < m_rowCount -1)
    {
        ++m_cursorRow;
        update();
        return;
    }

    if (m_disasm.size() > 0)
    {
        // Find our current line in disassembly
        for (int i = 0; i < m_disasm.size(); ++i)
        {
            if (m_disasm[i].address == m_logicalAddr)
            {
                // This will go off and request the memory itself
                SetAddress(m_disasm[i].GetEndAddr());
                return;
            }
        }
        // Default to line 1
        SetAddress(m_disasm[0].GetEndAddr());
    }
}

void DisasmWidget::PageUp()
{
    if (m_cursorRow != 0)
    {
        m_cursorRow = 0;
        update();
        return;
    }

    if (m_requestId != 0)
        return; // not up to date

    // TODO we should actually disassemble upwards to see if something sensible appears
    uint32_t moveSize = m_minInstSize * static_cast<uint32_t>(m_rowCount);
    if (m_logicalAddr > moveSize)
        SetAddress(m_logicalAddr - moveSize);
    else
        SetAddress(0);
}

void DisasmWidget::PageDown()
{
    if (m_rowCount > 0 && m_cursorRow < m_rowCount - 1)
    {
        // Just move to the bottom row
        m_cursorRow = m_rowCount - 1;
        update();
        return;
    }

    if (m_requestId != 0)
        return; // not up to date

    if (m_disasm.size() > 0)
    {
        // This will go off and request the memory itself
        SetAddress(m_disasm.back().GetEndAddr());
    }
}

void DisasmWidget::MouseScrollUp()
{
    if (m_requestId != 0)
        return; // not up to date

    // Mouse wheel moves a fixed number of bytes so up/down comes back to the same place
    uint32_t moveSize = m_minInstSize * static_cast<uint32_t>(m_rowCount);
    if (m_logicalAddr > moveSize)
        SetAddress(m_logicalAddr - moveSize);
    else
        SetAddress(0);
}

void DisasmWidget::MouseScrollDown()
{
    if (m_requestId != 0)
        return; // not up to date

    // Mouse wheel moves a fixed number of bytes so up/down comes back to the same place
    uint32_t moveSize = m_minInstSize * static_cast<uint32_t>(m_rowCount);
    SetAddress(m_logicalAddr + moveSize);
}

void DisasmWidget::RunToRow(int row)
{
    if (row >= 0 && row < m_disasm.size())
    {
        Line& line = m_disasm[row];
        m_pDispatcher->RunToPC(m_proc, line.address);
    }
}

void DisasmWidget::startStopChanged()
{
    // Request new memory for the view
    if (!m_pTargetModel->IsRunning())
    {
        // Decide what to request.
        if (m_bFollowPC)
        {
            // Decide if the new PC is covered by the existing view range. If so, don't move
            uint32_t pc = GetPC();
            if (m_disasm.size() == 0)
            {
                SetAddress(pc);
                return;
            }

            if (m_disasm[0].address > pc ||
                m_disasm.back().address < pc + 10)
            {
                // Update to PC position
                SetAddress(pc);
                return;
            }
        }
        // Just request what we had already.
        RequestMemory();
    }
}

void DisasmWidget::connectChanged()
{
    if (!m_pTargetModel->IsConnected())
    {
        m_disasm.clear();
        m_branches.clear();
        m_rowCount = 0;
        m_rowTexts.clear();
        m_memory.Clear();
    }
}

void DisasmWidget::memoryChanged(int memorySlot, uint64_t commandId)
{
    if (memorySlot != m_memSlot)
        return;

    // Only update for the last request we added
    if (commandId != m_requestId)
        return;

    const Memory* pMemOrig = m_pTargetModel->GetMemory(m_memSlot);
    if (!pMemOrig)
        return;

    if (m_logicalAddr == kInvalid)
    {
        m_logicalAddr = pMemOrig->GetAddress();
        emit addressChanged(m_logicalAddr);
    }

    // Cache it for later
    m_memory = *pMemOrig;
    // Clear the request, to say we are up to date
    m_requestId = 0;

    if (m_pTargetModel->IsMainStateUpdating())
        return;     // will be done later
    CalcDisasm();

    update();
}

void DisasmWidget::breakpointsChanged(uint64_t /*commandId*/)
{
    // Cache data
    m_breakpoints = m_pTargetModel->GetBreakpoints();
    if (m_pTargetModel->IsMainStateUpdating())
        return;     // update will be done later
    CalcDisasm();
    update();
}

void DisasmWidget::symbolTableChanged(uint64_t /*commandId*/)
{
    // Don't copy here, just force a re-read
//    emit dataChanged(this->createIndex(0, 0), this->createIndex(m_rowCount - 1, kColCount));
    if (m_pTargetModel->IsMainStateUpdating())
        return;     // update will be done later
    CalcDisasm();
    update();
}

void DisasmWidget::otherMemoryChanged(uint32_t address, uint32_t size)
{
    // Do a re-request if our memory is touched
    uint32_t ourAddr = m_logicalAddr;
    uint32_t ourSize = static_cast<uint32_t>((m_rowCount * m_maxInstSize) + 100);
    if (Overlaps(ourAddr, ourSize, address, size))
        RequestMemory();
}

void DisasmWidget::profileChanged()
{
    if (m_pTargetModel->IsMainStateUpdating())
        return;     // update will be done later
    RecalcColumnWidths();
    CalcDisasm();
    update();
}

void DisasmWidget::mainStateCompleted()
{
    RecalcColumnWidths();
    CalcDisasm();
    update();
}

void DisasmWidget::configChanged()
{
    // Force back to CPU if there's no DSP
    if (!m_pTargetModel->IsDspActive())
        SetProc(kProcCpu);
}


void DisasmWidget::paintEvent(QPaintEvent* ev)
{
    QWidget::paintEvent(ev);

    // CAREFUL! This could lead to an infinite loop of redraws if we are not.
    RecalcRowCount();

    QPainter painter(this);
    const QPalette& pal = this->palette();

    painter.setFont(m_monoFont);
    QFontMetrics info(painter.fontMetrics());

    const int y_ascent = info.ascent();
    const int y_midLine = y_ascent - info.strikeOutPos();

    // Anything to show?
    if (m_disasm.size())
    {
        // Highlight the mouse row with a lighter fill
        if (m_mouseRow != -1)
        {
            painter.setPen(Qt::PenStyle::NoPen);
            painter.setBrush(palette().alternateBase());
            painter.drawRect(0, GetPixelFromRow(m_mouseRow), rect().width(), m_lineHeight);
        }

        // Highlight the cursor row
        QBrush cursorColour = hasFocus() ? pal.highlight() : pal.mid();
        if (m_cursorRow != -1)
        {
            painter.setPen(QPen(cursorColour, 1, Qt::PenStyle::DashLine));
            painter.setBrush(Qt::BrushStyle::NoBrush);
            painter.drawRect(0, GetPixelFromRow(m_cursorRow), rect().width(), m_lineHeight);
        }

        // Highlight the PC row with standard highlighting
        for (int row = 0; row < m_rowTexts.size(); ++row)
        {
            const RowText& t = m_rowTexts[row];
            if (!t.isPc)
                continue;
            painter.setPen(Qt::PenStyle::NoPen);
            painter.setBrush(cursorColour);
            painter.drawRect(0, GetPixelFromRow(row), rect().width(), m_lineHeight);
            break;
        }

        for (int col = 0; col < kNumColumns; ++col)
        {
            int x = m_columnLeft[col] * m_charWidth;
            int x2 = m_columnLeft[col + 1] * m_charWidth;

            // Clip the column to prevent overdraw
            painter.setClipRect(x, 0, x2 - x, height());
            for (int row = 0; row < m_rowTexts.size(); ++row)
            {
                painter.setPen(Qt::PenStyle::NoPen);

                const RowText& t = m_rowTexts[row];
                if (t.isPc)
                    painter.setPen(pal.highlightedText().color());
                else
                    painter.setPen(pal.text().color());

                int row_top_y = GetPixelFromRow(row);
                int text_y = y_ascent + row_top_y;

                switch (col)
                {
                case kSymbol:
                    painter.drawText(x, text_y, t.symbol);
                    break;
                case kAddress:
                    painter.drawText(x, text_y, t.address);
                    break;
                case kPC:
                    if (t.isPc)
                        painter.drawText(x, text_y, ">");
                    break;
                case kBreakpoint:
                    if (t.isBreakpoint)
                    {
                        // Y is halfway between text bottom and row top
                        int circle_y = row_top_y + y_midLine;
                        int circle_rad = (y_ascent - y_midLine);
                        painter.setBrush(Qt::red);
                        painter.drawEllipse(QPoint(x + circle_rad, circle_y), circle_rad, circle_rad);
                    }
                    break;
                case kHex:
                    if (m_bShowHex)
                        painter.drawText(x, text_y, t.hex);
                    break;
                case kCycles:
                    painter.drawText(x, text_y, t.cycles);
                    break;
                case kDisasm:
                    painter.drawText(x, text_y, t.disasm);
                    break;
                case kComments:
                    if (!t.isPc)
                        painter.setPen(Qt::darkGreen);
                    painter.drawText(x, text_y, t.comments);
                    break;
                }
            } // row
        }   // col
    }
    painter.setClipRect(this->rect());

    // Branches
    int x_base = m_columnLeft[kAddress] * m_charWidth - 2;
    painter.setBrush(pal.dark());

    int arrowWidth = m_charWidth;
    int arrowHeight = arrowWidth / 2;

    for (int i = 0; i < m_branches.size(); ++i)
    {
        const Branch& b = m_branches[i];
        painter.setPen(QPen(pal.dark().color(), b.start == m_mouseRow ? 3 : 1));

        int yStart = GetPixelFromRow(b.start) + y_midLine + 1;
        int yEnd   = GetPixelFromRow(b.stop) + y_midLine - 1;
        if (b.type == 1)
            yEnd = GetPixelFromRow(0);
        else if (b.type == 2)
            yEnd = GetPixelFromRow(m_rowCount);

        int x_left = x_base - m_charWidth * (2 + b.depth);

        QPoint points[4] = {
            QPoint(x_base, yStart),
            QPoint(x_left, yStart),
            QPoint(x_left, yEnd),
            QPoint(x_base, yEnd)
        };
        if (b.type == 0)
        {

            painter.drawPolyline(points, 4);
            QPoint arrowPoints[3] = {
                QPoint(x_base, yEnd),
                QPoint(x_base - arrowWidth, yEnd + arrowHeight),
                QPoint(x_base - arrowWidth, yEnd - arrowHeight)
            };
            painter.drawPolygon(arrowPoints, 3);
        }
        else {
            painter.drawPolyline(points, 3);
        }
    }

    // Border
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(pal.dark(), hasFocus() ? 6 : 2));
    painter.drawRect(this->rect());
}

void DisasmWidget::keyPressEvent(QKeyEvent* event)
{
    Qt::KeyboardModifiers modif = event->modifiers() & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
    if (m_pTargetModel->IsConnected())
    {
        // Handle keyboard shortcuts with scope here, since QShortcut is global
        if (modif == Qt::ControlModifier)
        {
            switch (event->key())
            {
                case Qt::Key_H:         runToCursor();            return;
                case Qt::Key_B:         toggleBreakpoint();       return;
                case Qt::Key_Space:     KeyboardContextMenu();    return;
                case Qt::Key_F10:       runToCursor();            return;
                default: break;
            }
        }
        else if (modif == Qt::NoModifier)
        {
            switch (event->key())
            {
            case Qt::Key_Up:         MoveUp();              return;
            case Qt::Key_Down:       MoveDown();            return;
            case Qt::Key_PageUp:     PageUp();              return;
            case Qt::Key_PageDown:   PageDown();            return;
            case Qt::Key_F9:         toggleBreakpoint();    return;
            default: break;
            }
        }
    }
    QWidget::keyPressEvent(event);
}

void DisasmWidget::mouseMoveEvent(QMouseEvent *event)
{
    QPointF localPos = QTEVENT_GET_LOCAL_POS(event);
    m_mouseRow = GetRowFromPixel(int(localPos.y()));
    if (m_mouseRow >= m_rowCount)
        m_mouseRow = -1;  // hide if off the bottom
    if (this->underMouse())
        update();       // redraw highlight

    QWidget::mouseMoveEvent(event);
}

void DisasmWidget::mousePressEvent(QMouseEvent *event)
{
    QPointF localPos = QTEVENT_GET_LOCAL_POS(event);
    if (event->button() == Qt::LeftButton)
    {
        int row = GetRowFromPixel(int(localPos.y()));
        if (row < m_rowCount)
        {
            m_cursorRow = row;
            if (this->underMouse())
                update();       // redraw highlight
        }
    }
}

void DisasmWidget::wheelEvent(QWheelEvent *event)
{
    if (!this->underMouse())
    {
        event->ignore();
        return;
    }

    // Accumulate delta for some mice
    m_wheelAngleDelta += event->angleDelta().y();
    if (m_wheelAngleDelta >= 15)
    {
        MouseScrollUp();
        m_wheelAngleDelta = 0;
    }
    else if (m_wheelAngleDelta <= -15)
    {
        MouseScrollDown();
        m_wheelAngleDelta = 0;
    }
    event->accept();
}

bool DisasmWidget::event(QEvent* ev)
{
    if (ev->type() == QEvent::Leave)
    {
        // overwrite handling of PolishRequest if any
        m_mouseRow = -1;
        update();
    }
    return QWidget::event(ev);
}

void DisasmWidget::CalcDisasm()
{
    if (m_proc == kProcCpu)
        CalcDisasm68();
    else
        CalcDisasm56();
}

void DisasmWidget::CalcDisasm68()
{
    // Make sure the data we get back matches our expectations...
    if (m_logicalAddr < m_memory.GetAddress())
        return;
    if (m_logicalAddr >= m_memory.GetAddress() + m_memory.GetSize())
        return;

    // Work out where we need to start disassembling from
    uint32_t offset = m_logicalAddr - m_memory.GetAddress();
    uint32_t size = m_memory.GetSize() - offset;
    hop68::buffer_reader disasmBuf(m_memory.GetData() + offset, size, m_memory.GetAddress() + offset);

    Disassembler::disassembly tmp;
    Disassembler::decode_buf(disasmBuf, tmp, m_pTargetModel->GetDisasmSettings(), m_logicalAddr, m_rowCount);

    // Convert to shared format
    size_t rowCount = tmp.lines.size();
    m_disasm.resize(rowCount);
    for (size_t i = 0; i < rowCount; ++i)
    {
        m_disasm[i].Reset();
        m_disasm[i].address = tmp.lines[i].address;
        m_disasm[i].inst68 = tmp.lines[i].inst;
        m_disasm[i].inst56.reset();
        m_disasm[i].proc = kProcCpu;
        memcpy(m_disasm[i].mem, tmp.lines[i].mem, 32);
    }

    // Annotate traps
    CalcAnnotations68();

    // Recalc Text (which depends on e.g. symbols
    m_rowTexts.clear();
    m_branches.clear();
    if (m_disasm.size() == 0)
        return;

     // NOTE: do not use m_rowCount here, it can be stale!
    for (int row = 0; row < m_disasm.size(); ++row)
    {
        RowText t;
        Line& line = m_disasm[row];

        // Address
        uint32_t addr = line.address;
        t.address = QString::asprintf("%08x", addr);
        t.isPc = line.address == GetPC();
        t.isBreakpoint = false;

        // Symbol
        QString addrText;
        Symbol sym;
        if (row == 0)
        {
            // show symbol + offset if necessary for the top line
            t.symbol = DescribeSymbol(m_pTargetModel->GetSymbolTable(), addr);
            if (!t.symbol.isEmpty())
                t.symbol += ":";
        }
        else {
            if (m_pTargetModel->GetSymbolTable().Find(addr, sym))
                t.symbol = QString::fromStdString(sym.name) + ":";
        }

        // Hex
        for (uint32_t i = 0; i < line.GetByteSize(); ++i)
            t.hex += QString::asprintf("%02x", line.mem[i]);

        // Cycles
        if (m_pTargetModel->IsProfileEnabled())
        {
            uint32_t count, cycles;
            m_pTargetModel->GetProfileData(addr, count, cycles);
            if (count)
            {
                 if (m_pSession->GetSettings().m_profileDisplayMode == Session::Settings::kTotal)
                     t.cycles = QString::asprintf("%d/%d", count, cycles);
                 else if (m_pSession->GetSettings().m_profileDisplayMode == Session::Settings::kMean)
                     t.cycles = QString::asprintf("%d*%d", count, cycles/count);
            }
        }

        // Disassembly
        QTextStream ref(&t.disasm);
        Disassembler::print(line.inst68, line.address, ref, m_pSession->GetSettings().m_bDisassHexNumerics);

        // Comments
        QTextStream refC(&t.comments);
        Registers regs = m_pTargetModel->GetRegs();
        printEA(line.inst68.op0, regs, line.address, refC);
        if (t.comments.size() != 0)
            refC << "  ";
        printEA(line.inst68.op1, regs, line.address, refC);

        refC << line.annotations.osComments;

        // Breakpoint/PC
        for (size_t i = 0; i < m_breakpoints.m_breakpoints.size(); ++i)
        {
            const Breakpoint& bp = m_breakpoints.m_breakpoints[i];
            if (bp.m_proc == 0 && bp.m_pcHack == line.address)
            {
                t.isBreakpoint = true;
                break;
            }
        }

        // Branch info
        uint32_t target;
        if (DisAnalyse::getBranchTarget(line.address, line.inst68, target))
            (void)AddDisasmBranch(row, target, false);
        m_rowTexts.push_back(t);
    }

    LayOutBranches();
}

void DisasmWidget::CalcDisasm56()
{
    // Make sure the data we get back matches our expectations...
    if (m_logicalAddr < m_memory.GetAddress())
        return;
    if (m_logicalAddr >= m_memory.GetAddress() + m_memory.GetSize() / 3)
        return;

    // Work out where we need to start disassembling from
    uint32_t addrOffset = (m_logicalAddr - m_memory.GetAddress());
    uint32_t size = m_memory.GetSize() - addrOffset * 3;
    hop56::buffer_reader disasmBuf(m_memory.GetData() + addrOffset * 3, size, m_memory.GetAddress() + addrOffset);

    Disassembler56::disassembly tmp;
    hop56::decode_settings dummy;
    Disassembler56::decode_buf(disasmBuf, tmp, dummy, m_logicalAddr, m_rowCount);

    // Convert to shared format
    int rowCount = tmp.lines.size();
    m_disasm.resize(rowCount);
    for (int i = 0; i < rowCount; ++i)
    {
        m_disasm[i].Reset();
        m_disasm[i].address = tmp.lines[i].address;
        m_disasm[i].inst68.reset();
        m_disasm[i].inst56 = tmp.lines[i].inst;
        m_disasm[i].proc = kProcDsp;
        memcpy(m_disasm[i].mem, tmp.lines[i].mem, sizeof(tmp.lines[i].mem));
    }

    m_rowTexts.clear();
    m_branches.clear();

    if (m_disasm.size() == 0)
        return;

    // Annotate traps
    CalcAnnotations56();

    const SymbolTable& symsP = m_pTargetModel->GetSymbolTable(MEM_P);

    // NOTE: do not use m_rowCount here, it can be stale!
    for (int row = 0; row < m_disasm.size(); ++row)
    {
        RowText t;
        Line& line = m_disasm[row];

        // Address
        uint32_t addr = line.address;
        t.address = QString::asprintf("P:$%04x", addr);
        t.isPc = line.address == m_pTargetModel->GetStartStopPC(kProcDsp);
        t.isBreakpoint = false;
        t.hex.clear();
        t.symbol.clear();
        t.cycles.clear();

        // Hex
        for (uint32_t i = 0; i < line.GetByteSize(); ++i)
            t.hex += QString::asprintf("%02x", line.mem[i]);

        // Breakpoint/PC
        for (size_t i = 0; i < m_breakpoints.m_breakpoints.size(); ++i)
        {
            const Breakpoint& bp = m_breakpoints.m_breakpoints[i];
            if (bp.m_proc == 1 && bp.m_pcHack == line.address)
            {
                t.isBreakpoint = true;
                break;
            }
        }

        // Symbol
        QString addrText;
        Symbol sym;
        if (row == 0)
        {
            // show symbol + offset if necessary for the top line
            t.symbol = DescribeSymbol(symsP, addr);
            if (!t.symbol.isEmpty())
                t.symbol += ":";
        }
        else {
            if (symsP.Find(addr, sym))
                t.symbol = QString::fromStdString(sym.name) + ":";
        }
#if 0
        // Cycles
        if (m_pTargetModel->IsProfileEnabled())
        {
            uint32_t count, cycles;
            m_pTargetModel->GetProfileData(addr, count, cycles);
            if (count)
            {
                 if (m_pSession->GetSettings().m_profileDisplayMode == Session::Settings::kTotal)
                     t.cycles = QString::asprintf("%d/%d", count, cycles);
                 else if (m_pSession->GetSettings().m_profileDisplayMode == Session::Settings::kMean)
                     t.cycles = QString::asprintf("%d*%d", count, cycles/count);
            }
        }
#endif

        // Disassembly
        QTextStream ref(&t.disasm);
        Disassembler56::print_inst(line.inst56, ref);

        // Comments
        QTextStream refC(&t.comments);
        bool wrotePrev = false;
        for (uint32_t i = 0; i < Line::Annotations::kNumEAs; ++i)
        {
            if (line.annotations.valid[i])
            {
                if (wrotePrev)
                    refC << ", ";
                else
                    refC << "; ";

                const MemAddr& addr = line.annotations.address[i];
                refC << Format::to_address(addr);

                const SymbolTable& symTab = m_pTargetModel->GetSymbolTable(addr.space);
                Symbol sym;
                if (symTab.FindLowerOrEqual(addr.addr, true, sym))
                    refC << "=" << sym.name.c_str();
                wrotePrev = true;
            }
        }
        refC << line.annotations.osComments;

        // Breakpoint/PC
        for (size_t i = 0; i < m_breakpoints.m_breakpoints.size(); ++i)
        {
            if (m_breakpoints.m_breakpoints[i].m_pcHack == line.address)
            {
                t.isBreakpoint = true;
                break;
            }
        }

        // Branch info
        uint32_t target;
        bool reversed = false;
        if (DisAnalyse56::getBranchTarget(line.inst56, line.address, target, reversed))
            (void) AddDisasmBranch(row, target, reversed);
        m_rowTexts.push_back(t);
    }

    LayOutBranches();

}

// Add a branch to m_branches. We try to match the target address
// to existing row addresses in the disassembly
int DisasmWidget::AddDisasmBranch(int row, uint32_t target, bool reversed)
{
    int targetLine = -1;

    uint32_t topAddr = m_disasm[0].address;
    uint32_t lastAddr = m_disasm.back().address;
    Branch b;
    b.depth = 0;
    b.type = 0;
    b.start = row;
    b.stop = -1;

    // Try to find the existing line
    if (target < topAddr)
    {
        b.stop = 0;
        b.type = 1; // top
        targetLine = -1;
    }
    else if (target > lastAddr)
    {
        b.stop = m_disasm.size() - 1;
        b.type = 2; // bottom
        targetLine = -1;
    }
    else
    {
        // Search the rows for a match
        for (int i = 0; i < m_disasm.count(); ++i)
        {
            if (m_disasm[i].address == target)
            {
                targetLine = i;
                b.stop = targetLine;
                b.type = 0;
                break;
            }
        }
        // Note that we might have a branch target in the disasm
        // range, but not one of the rows!
    }

    if (b.stop != -1)
    {
        assert(b.start != -1);

        if (reversed)
        {
            int tmp = b.start;
            b.start = b.stop;
            b.stop = tmp;
        }
        m_branches.push_back(b);
    }
    return targetLine;
}

// Calculate the X-offsets of where each branch is drawn.
// This tries to minimize the "crossing" of branch lines.
void DisasmWidget::LayOutBranches()
{
    // Branch layout:
    // Method is simple but seems to work OK: iteratively choose the branch that
    // crosses as few other branch start/stops as possible.
    // Then we work out the "depth" from all pre-existing lines inside our bounds,
    // and add 1.
    // This might need tweaking for branches that go off the top/bottom, but seems OK
    // so far.

    // Count the start/stopping lines.
    // Previously we tracked start and stop separately, but we
    // only ever use them added together
    QVector<int> startsAndStops(m_disasm.size());
    for (int i = 0; i < m_branches.size(); ++i)
    {
        const Branch& b = m_branches[i];
        if (b.stop != -1)
        {
            startsAndStops[b.start]++;
            startsAndStops[b.stop]++;
        }
    }

    // This stores the minimum distance for each row in the disassembly
    QVector<int> depths(m_disasm.size());
    for (int write = 0; write < m_branches.size(); ++write)
    {
        // Choose the remaining branch with the lowest "score", where "score" is
        // the number of branch start/stops we cross (including our own, but this
        // cancels out among all branches)
        int lowestScore = INT_MAX;
        int lowestId = -1;

        for (int i = write; i < m_branches.size(); ++i)
        {
            const Branch& b = m_branches[i];
            int score = 0;
            for (int r = b.top(); r <= b.bottom(); ++r)
                score += startsAndStops[r];

            if (score < lowestScore)
            {
                lowestScore = score;
                lowestId = i;
            }
        }

        // Work out our minimum "depth" needed to avoid all the existing branches
        // we straddle
        Branch& chosen = m_branches[lowestId];
        int depth = 0;
        for (int r = chosen.top(); r <= chosen.bottom(); ++r)
            depth = std::max(depth, depths[r]);

        // Write this depth back into our range
        for (int r = chosen.top(); r <= chosen.bottom(); ++r)
            depths[r] = depth + 1;
        chosen.depth = depth;

        // Swap entries "write" and "chosen"
        Branch tmp = m_branches[write];
        m_branches[write] = m_branches[lowestId];
        m_branches[lowestId] = tmp;
    }
}

// Generate the Effective Addresses for the row,
// and any annotation text like traps etc.
void DisasmWidget::CalcAnnotations68()
{
    // Precalc EAs
    const Registers& regs = m_pTargetModel->GetRegs();
    uint32_t pc = regs.Get(Registers::PC);
    for (int i = 0; i < m_disasm.size(); ++i)
    {
        Line& line = m_disasm[i];
        const hop68::instruction& inst = line.inst68;
        Line::Annotations& annots = line.annotations;
        bool useRegs = inst.address == pc;

        annots.valid[0] = Disassembler::calc_fixed_ea(inst.op0, useRegs, regs, m_disasm[i].address, annots.address[0].addr);
        annots.valid[1] = Disassembler::calc_fixed_ea(inst.op1, useRegs, regs, m_disasm[i].address, annots.address[1].addr);
        annots.valid[2] = false;
        annots.valid[3] = false;
        annots.osComments = GetTOSAnnotation(m_memory, line.address, inst);
    }
}

void DisasmWidget::CalcAnnotations56()
{
    for (int i = 0; i < m_disasm.size(); ++i)
    {
        Line& line = m_disasm[i];
        const hop56::instruction& inst = line.inst56;
        Line::Annotations& annots = line.annotations;

        bool valids[10];
        Disassembler56::addr_t addr[10];
        valids[0] = Disassembler56::calc_ea(inst.operands[0], addr[0]);
        valids[1] = Disassembler56::calc_ea(inst.operands[1], addr[1]);
        valids[2] = Disassembler56::calc_ea(inst.operands[2], addr[2]);
        valids[3] = Disassembler56::calc_ea(inst.operands2[0], addr[3]);
        valids[4] = Disassembler56::calc_ea(inst.operands2[1], addr[4]);
        valids[5] = Disassembler56::calc_ea(inst.operands2[2], addr[5]);
        valids[6] = Disassembler56::calc_ea(inst.pmoves[0].operands[0], addr[6]);
        valids[7] = Disassembler56::calc_ea(inst.pmoves[0].operands[1], addr[7]);
        valids[8] = Disassembler56::calc_ea(inst.pmoves[1].operands[0], addr[8]);
        valids[9] = Disassembler56::calc_ea(inst.pmoves[1].operands[1], addr[9]);

        uint32_t readSlot = 0;
        uint32_t writeSlot = 0;
        static const MemSpace spaces[hop56::MEM_COUNT] =
        {
            MEM_P,        // "None" maps to P: memory
            MEM_X,
            MEM_Y,
            MEM_P,
            MEM_L
        };

        while (writeSlot < Line::Annotations::kNumEAs && readSlot < 8)
        {
            if (valids[readSlot])
            {
                // Convert to our memory space format
                const Disassembler56::addr_t& a = addr[readSlot];
                annots.valid[writeSlot] = true;
                annots.address[writeSlot] = maddr(spaces[a.mem], a.addr);
                ++writeSlot;
            }
            ++readSlot;
        }
    }
}

void DisasmWidget::ToggleBreakpoint(int row)
{
    // set a breakpoint
    if (row < 0 || row >= m_disasm.size())
        return;

    Line& line = m_disasm[row];
    uint32_t addr = line.address;
    bool removed = false;

    const Breakpoints& bps = m_pTargetModel->GetBreakpoints();
    for (size_t i = 0; i < bps.m_breakpoints.size(); ++i)
    {
        // NO CHECK test CPU type
        const Breakpoint& bp = bps.m_breakpoints[i];
        if (bp.m_proc == m_proc && bp.m_pcHack == addr)
        {
            m_pDispatcher->DeleteBreakpoint(bp.m_proc, bp.m_id);
            removed = true;
        }
    }
    if (!removed)
    {
        QString cmd = QString::asprintf("pc = $%x", addr);
        m_pDispatcher->SetBreakpoint(m_proc, cmd.toStdString().c_str(), Dispatcher::kBpFlagNone);
    }
}

void DisasmWidget::SetPC(int row)
{
    if (row < 0 || row >= m_disasm.size())
        return;

    Line& line = m_disasm[row];
    uint32_t addr = line.address;
    if (m_proc == kProcCpu)
        m_pDispatcher->SetRegister(kProcCpu, Registers::PC, addr);
    else
        m_pDispatcher->SetRegister(kProcDsp, DspRegisters::PC, addr);

    // Re-request values so that main window is updated
    m_pDispatcher->ReadRegisters();
}

void DisasmWidget::NopRow(int row)
{
    if (row >= m_disasm.size())
        return;

    Line& line = m_disasm[row];
    uint32_t addr = line.address;

    QVector<uint8_t> data;

    for (int i = 0; i <  line.inst68.byte_count / 2; ++i)
    {
        data.push_back(0x4e);
        data.push_back(0x71);
    }
    m_pDispatcher->WriteMemory(addr, data);
}

void DisasmWidget::SetRowCount(int count)
{
    if (count < 1)
        count = 1;
    if (count != m_rowCount)
    {
        m_rowCount = count;

        // Do we need more data?
        CalcDisasm();
        if (m_disasm.size() < m_rowCount)
        {
            // We need more memory
            RequestMemory();
        }
    }
}

void DisasmWidget::SetShowHex(bool show)
{
    m_bShowHex = show;
    RecalcColumnWidths();
    update();
}

void DisasmWidget::SetFollowPC(bool bFollow)
{
    m_bFollowPC = bFollow;
    if (bFollow)
        SetAddress(GetPC());
    update();
}

void DisasmWidget::SetProc(Processor mode)
{
    // Don't do the messy stuff if already in the right mode
    if (m_proc == mode)
        return;

    // Remember address before we switch out of the mode.
    m_lastAddress[m_proc] = m_logicalAddr;

    // Set up per-mode settings before requesting the memory address.
    m_proc = mode;
    if (mode == kProcCpu)
    {
        m_minInstSize = 2;
        m_maxInstSize = 32;
    }
    else
    {
        m_minInstSize = 1;
        m_maxInstSize = 2;
    }

    uint32_t newAddr = m_lastAddress[mode];
    if (m_bFollowPC)
        SetAddress(GetPC());
    else
        SetAddress(newAddr);

    RecalcColumnWidths();
    emit procChangedSignal();
}

void DisasmWidget::printEA(const hop68::operand& op, const Registers& regs, uint32_t address, QTextStream& ref) const
{
    uint32_t ea;

    // Only do a full analysis if this is the PC and therefore the registers are valid...
    if (Disassembler::calc_fixed_ea(op, address == m_pTargetModel->GetStartStopPC(kProcCpu), regs, address, ea))
    {
        ea &= 0xffffff; // mask for ST hardware range
        ref << "$" << QString::asprintf("%x", ea);
        QString sym = DescribeSymbol(m_pTargetModel->GetSymbolTable(), ea);
        if (!sym.isEmpty())
           ref << " " << sym;
    };
}

void DisasmWidget::contextMenuEvent(QContextMenuEvent *event)
{
    int row = GetRowFromPixel(event->y());
    if (row < 0 || row >= m_rowTexts.size())
        return;
    ContextMenu(row, event->globalPos());
}

void DisasmWidget::runToCursorRightClick()
{
    RunToRow(m_rightClickRow);
    m_rightClickRow = -1;
}

void DisasmWidget::toggleBreakpointRightClick()
{
    ToggleBreakpoint(m_rightClickRow);
    m_rightClickRow = -1;
}

void DisasmWidget::setPCRightClick()
{
    SetPC(m_rightClickRow);
    m_rightClickRow = -1;
}

void DisasmWidget::nopRightClick()
{
    NopRow(m_rightClickRow);
    m_rightClickRow = -1;
}

//-----------------------------------------------------------------------------
void DisasmWidget::copyRightClick()
{
    CalcDisasm();
    QClipboard *clipboard = QGuiApplication::clipboard();
    QString newText;

    QTextStream ref(&newText);
    for (int i = 0; i < m_rowTexts.size(); ++i)
    {
        ref.setFieldAlignment(QTextStream::FieldAlignment::AlignLeft);
        const RowText& line = m_rowTexts[i];
        if (line.symbol.size() != 0)
        {
            ref << line.symbol;
            ref.setFieldWidth(0);
            ref << Format::endl;
        }

        ref.setFieldWidth(15);
        ref << QString("$") + line.address;
        ref.setFieldWidth(30);
        ref << line.disasm;

        if (line.comments.size() != 0)
        {
            ref.setFieldWidth(0);
            ref << QString("; ") + line.comments;
        }

        ref.setFieldWidth(0);
        ref << Format::endl;
    }
    clipboard->setText(newText);
    m_pSession->SetMessage("Disassembly copied to clipboard.");
}

void DisasmWidget::settingsChangedSlot()
{
    UpdateFont();
    RecalcRowCount();
    RequestMemory();
    update();
}

void DisasmWidget::runToCursor()
{
    if (m_cursorRow != -1)
        RunToRow(m_cursorRow);
}

void DisasmWidget::toggleBreakpoint()
{
    if (m_cursorRow != -1)
        ToggleBreakpoint(m_cursorRow);
}

void DisasmWidget::resizeEvent(QResizeEvent* )
{
    RecalcRowCount();
}

void DisasmWidget::RecalcRowCount()
{
    int h = this->rect().height() - Session::kWidgetBorderY * 2;
    int rowh = m_lineHeight;
    if (rowh != 0)
        SetRowCount(h / rowh);
}

void DisasmWidget::UpdateFont()
{
    m_monoFont = m_pSession->GetSettings().m_font;
    QFontMetrics info(m_monoFont);
    m_lineHeight = info.lineSpacing();
    m_charWidth = info.horizontalAdvance("0");
}

void DisasmWidget::RecalcColumnWidths()
{
    int pos = 1;
    m_columnLeft[kSymbol] = pos; pos += 19;
    m_columnLeft[kAddress] = pos;
    if (m_proc == kProcCpu)
        pos += 9;
    else
        pos += 7;
    m_columnLeft[kPC] = pos; pos += 1;
    m_columnLeft[kBreakpoint] = pos; pos += 1;
    m_columnLeft[kHex] = pos; pos += (m_bShowHex) ? (10 * 2 + 1) : 0;
    m_columnLeft[kCycles] = pos;
    pos += (m_pTargetModel->IsProfileEnabled()) ? 20 : 0;

    m_columnLeft[kDisasm] = pos;
    if (m_proc == kProcCpu)
        pos += 8+18+9+1; // movea.l $12345678(pc,d0.w),$12345678
    else
        pos += 42;       // best guess for the moment

    m_columnLeft[kComments] = pos; pos += 80;
    m_columnLeft[kNumColumns] = pos;
}

int DisasmWidget::GetPixelFromRow(int row) const
{
    return Session::kWidgetBorderY + row * m_lineHeight;
}

int DisasmWidget::GetRowFromPixel(int y) const
{
    if (!m_lineHeight)
        return 0;
    return (y - Session::kWidgetBorderY) / m_lineHeight;
}

void DisasmWidget::KeyboardContextMenu()
{
    ContextMenu(m_cursorRow,
                   mapToGlobal(QPoint(100, GetPixelFromRow(m_cursorRow))));
}

void DisasmWidget::ContextMenu(int row, QPoint globalPos)
{
    m_rightClickRow = row;

    // Right click menus are instantiated on demand, so we can dynamically add to them
    QMenu menu(this);

    // Add the default actions
    menu.addAction(m_pRunUntilAction);
    menu.addAction(m_pBreakpointAction);
    menu.addAction(m_pSetPcAction);
    menu.addMenu(m_pEditMenu);

    // Set up relevant menu items
    MemSpace space = (m_proc == kProcCpu) ? MEM_CPU : MEM_P;
    uint32_t instAddr;
    bool vis = GetInstructionAddr(m_rightClickRow, instAddr);
    if (vis)
    {
        m_showAddressMenus[0].Set("This instruction",
                                       m_pSession, space, instAddr);
        m_showAddressMenus[0].AddTo(&menu);
    }

    for (uint32_t op = 0; op < Line::Annotations::kNumEAs; ++op)
    {
        int32_t menuIndex = op + 1;
        MemAddr opAddr;
        if (GetEA(m_rightClickRow, op, opAddr))
        {
            // This now handles different memory spaces
            m_showAddressMenus[menuIndex].Set(
                        QString::asprintf("Effective address %u", menuIndex),
                        m_pSession, opAddr.space, opAddr.addr);
            m_showAddressMenus[menuIndex].AddTo(&menu);
        }
    }

    menu.addAction(m_pSearchAction);
    menu.addAction(m_pCopyAction);

    // Run it
    menu.exec(globalPos);
}

uint32_t DisasmWidget::GetPC() const
{
    return m_pTargetModel->GetStartStopPC(m_proc);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

DisasmWindow::DisasmWindow(QWidget *parent, Session* pSession, int windowIndex) :
    QDockWidget(parent),
    m_pSession(pSession),
    m_pTargetModel(pSession->m_pTargetModel),
    m_pDispatcher(pSession->m_pDispatcher),
    m_windowIndex(windowIndex),
    m_searchRequestId(0)
{
    QString key = QString::asprintf("DisasmView%d", m_windowIndex);
    setObjectName(key);

    // Search action created first so we can pass it to the child widget
    QAction* pMenuSearchAction = new QAction("Search...", this);
    connect(pMenuSearchAction, &QAction::triggered, this, &DisasmWindow::findClickedSlot);

    // Construction. Do in order of tabbing
    m_pDisasmWidget = new DisasmWidget(this, pSession, windowIndex, pMenuSearchAction);
    m_pDisasmWidget->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));

    // TODO: reduce footprint of this button
    m_pProcButton = new QPushButton(this);

    m_pAddressEdit = new QLineEdit(this);
    m_pFollowPC = new QCheckBox("Follow PC", this);
    m_pFollowPC->setTristate(false);
    m_pFollowPC->setChecked(m_pDisasmWidget->GetFollowPC());
    m_pShowHex = new QCheckBox("Show hex", this);
    m_pShowHex->setTristate(false);
    m_pShowHex->setChecked(m_pDisasmWidget->GetShowHex());

    // Layouts
    QVBoxLayout* pMainLayout = new QVBoxLayout;
    QHBoxLayout* pTopLayout = new QHBoxLayout;
    auto pMainRegion = new QWidget(this);   // whole panel
    auto pTopRegion = new QWidget(this);    // top buttons/edits
    //pMainGroupBox->setFlat(true);

    SetMargins(pTopLayout);
    pTopLayout->addWidget(m_pProcButton);
    pTopLayout->addWidget(m_pAddressEdit);
    pTopLayout->addWidget(m_pFollowPC);
    pTopLayout->addWidget(m_pShowHex);

    SetMargins(pMainLayout);
    pMainLayout->addWidget(pTopRegion);
    pMainLayout->addWidget(m_pDisasmWidget);
    pMainLayout->setAlignment(Qt::Alignment(Qt::AlignTop));

    pTopRegion->setLayout(pTopLayout);
    pMainRegion->setLayout(pMainLayout);
    setWidget(pMainRegion);

    m_pSymbolTableModel = new SymbolTableModel(this, m_pTargetModel->GetSymbolTable());
    QCompleter* pCompl = new QCompleter(m_pSymbolTableModel, this);
    pCompl->setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);
    m_pAddressEdit->setCompleter(pCompl);

    // Now that everything is set up we can load the setings
    loadSettings();

    // The scope here is explained at https://forum.qt.io/topic/67981/qshortcut-multiple-widget-instances/2
    new QShortcut(QKeySequence("Ctrl+F"),         this, SLOT(findClickedSlot()), nullptr, Qt::WidgetWithChildrenShortcut);
    new QShortcut(QKeySequence("F3"),             this, SLOT(nextClickedSlot()), nullptr, Qt::WidgetWithChildrenShortcut);
    new QShortcut(QKeySequence("Ctrl+G"),         this, SLOT(gotoClickedSlot()), nullptr, Qt::WidgetWithChildrenShortcut);
    new QShortcut(QKeySequence("Ctrl+L"),         this, SLOT(lockClickedSlot()), nullptr, Qt::WidgetWithChildrenShortcut);

    // Listen for start/stop, so we can update our memory request
    connect(m_pDisasmWidget,&DisasmWidget::addressChanged,            this, &DisasmWindow::UpdateTextBox);
    connect(m_pDisasmWidget,&DisasmWidget::procChangedSignal,         this, &DisasmWindow::syncUiButtons);
    connect(m_pProcButton,  &QPushButton::clicked,                    this, &DisasmWindow::procChangedClicked);
    connect(m_pAddressEdit, &QLineEdit::returnPressed,                this, &DisasmWindow::returnPressedSlot);
    connect(m_pAddressEdit, &QLineEdit::textEdited,                   this, &DisasmWindow::textChangedSlot);
    connect(m_pFollowPC,    &QCheckBox::stateChanged,                 this, &DisasmWindow::followPCClickedSlot);
    connect(m_pShowHex,     &QCheckBox::stateChanged,                 this, &DisasmWindow::showHexClickedSlot);
    connect(m_pSession,     &Session::addressRequested,               this, &DisasmWindow::requestAddress);
    connect(m_pTargetModel, &TargetModel::searchResultsChangedSignal, this, &DisasmWindow::searchResultsSlot);
    connect(m_pTargetModel, &TargetModel::symbolTableChangedSignal,   this, &DisasmWindow::symbolTableChangedSlot);
    connect(m_pTargetModel, &TargetModel::configChangedSignal,        this, &DisasmWindow::syncUiButtons);

    syncUiButtons();
    this->resizeEvent(nullptr);
}

void DisasmWindow::requestAddress(Session::WindowType type, int windowIndex, int memorySpace, uint32_t address)
{
    if (type != Session::WindowType::kDisasmWindow)
        return;

    if (windowIndex != m_windowIndex)
        return;

    if (memorySpace == MEM_CPU)
        // Requesting CPU memory
        SetProc(kProcCpu);
    else
        // DSP
        SetProc(kProcDsp);

    m_pDisasmWidget->SetAddress(std::to_string(address));
    setVisible(true);
    raise();
    this->keyFocus();
}

void DisasmWindow::keyFocus()
{
    activateWindow();
    m_pDisasmWidget->setFocus();
}

void DisasmWindow::loadSettings()
{
    QSettings settings;
    QString key = QString::asprintf("DisasmView%d", m_windowIndex);
    settings.beginGroup(key);

    //restoreGeometry(settings.value("geometry").toByteArray());
    m_pDisasmWidget->SetShowHex(settings.value("showHex", QVariant(true)).toBool());
    m_pDisasmWidget->SetFollowPC(settings.value("followPC", QVariant(true)).toBool());
    Processor mode = static_cast<Processor>(settings.value("processor", QVariant(kProcCpu)).toInt());
    SetProc(mode);

    m_pShowHex->setChecked(m_pDisasmWidget->GetShowHex());
    m_pFollowPC->setChecked(m_pDisasmWidget->GetFollowPC());
    settings.endGroup();
}

void DisasmWindow::saveSettings()
{
    QSettings settings;
    QString key = QString::asprintf("DisasmView%d", m_windowIndex);
    settings.beginGroup(key);

    //settings.setValue("geometry", saveGeometry());
    settings.setValue("showHex", m_pDisasmWidget->GetShowHex());
    settings.setValue("followPC", m_pDisasmWidget->GetFollowPC());
    settings.setValue("processor", m_pDisasmWidget->GetProc());
    settings.endGroup();
}

void DisasmWindow::keyDownPressed()
{
    m_pDisasmWidget->MoveDown();
}

void DisasmWindow::keyUpPressed()
{
    m_pDisasmWidget->MoveUp();
}

void DisasmWindow::keyPageDownPressed()
{
    m_pDisasmWidget->PageDown();
}

void DisasmWindow::keyPageUpPressed()
{
    m_pDisasmWidget->PageUp();
}

void DisasmWindow::procChangedClicked()
{
    Processor mode = m_pDisasmWidget->GetProc();
    if (mode == kProcCpu)
        SetProc(kProcDsp);
    else
        SetProc(kProcCpu);
}

void DisasmWindow::returnPressedSlot()
{
    bool success = m_pDisasmWidget->SetAddress(m_pAddressEdit->text().toStdString());
    Colouring::SetErrorState(m_pAddressEdit, success);
    if (success)
    {
        m_pDisasmWidget->setFocus();
    }
}

void DisasmWindow::textChangedSlot()
{
    uint32_t addr;
    bool success = StringParsers::ParseCpuExpression(m_pAddressEdit->text().toStdString().c_str(), addr,
                                       m_pTargetModel->GetSymbolTable(), m_pTargetModel->GetRegs());
    Colouring::SetErrorState(m_pAddressEdit, success);
}

void DisasmWindow::showHexClickedSlot()
{
    m_pDisasmWidget->SetShowHex(m_pShowHex->isChecked());
}

void DisasmWindow::followPCClickedSlot()
{
    m_pDisasmWidget->SetFollowPC(m_pFollowPC->isChecked());
}

void DisasmWindow::UpdateTextBox()
{
    uint32_t addr = m_pDisasmWidget->GetAddress();
    m_pAddressEdit->setText(QString::asprintf("$%x", addr));
}

void DisasmWindow::findClickedSlot()
{
    if (!m_pTargetModel->IsConnected())
        return;

    uint32_t addr = 0;
    if (!m_pDisasmWidget->GetAddressAtCursor(addr))
        return;

    {
        // Fill in the "default"
        m_searchSettings.m_startAddress = addr;
        if (m_searchSettings.m_endAddress == 0)
            m_searchSettings.m_endAddress = m_pTargetModel->GetSTRamSize();

        SearchDialog search(this, m_pTargetModel, m_searchSettings);
        search.setModal(true);
        int code = search.exec();
        if (code == QDialog::DialogCode::Accepted &&
            m_pTargetModel->IsConnected())
        {
            m_searchRequestId = m_pDispatcher->SendMemFind(m_searchSettings.m_masksAndValues,
                                     m_searchSettings.m_startAddress,
                                     m_searchSettings.m_endAddress);
            m_pSession->SetMessage(QString("Searching: " + m_searchSettings.m_originalText));
        }
    }
}

void DisasmWindow::nextClickedSlot()
{
    if (!m_pTargetModel->IsConnected())
        return;

    uint32_t addr;
    if (!m_pDisasmWidget->GetAddressAtCursor(addr))
        return;

    if (m_searchSettings.m_masksAndValues.size() != 0)
    {
        // Start address should already have been filled
        {
            m_searchRequestId = m_pDispatcher->SendMemFind(m_searchSettings.m_masksAndValues,
                                     addr + 1,
                                     m_searchSettings.m_endAddress);
        }
    }
}

void DisasmWindow::gotoClickedSlot()
{
    m_pAddressEdit->setFocus();
}

void DisasmWindow::lockClickedSlot()
{
    m_pFollowPC->toggle();
}

void DisasmWindow::searchResultsSlot(uint64_t responseId)
{
    if (responseId == m_searchRequestId)
    {
        const SearchResults& results = m_pTargetModel->GetSearchResults();
        if (results.addresses.size() > 0)
        {
            uint32_t addr = results.addresses[0];
            m_pDisasmWidget->SetSearchResultAddress(addr);

            // Allow the "next" operation to work
            m_searchSettings.m_startAddress = addr + 1;
            m_pDisasmWidget->setFocus();
            m_pSession->SetMessage(QString("String '%1' found at %2").
                                   arg(m_searchSettings.m_originalText).
                                   arg(Format::to_hex32(addr)));
        }
        else
        {
            m_pSession->SetMessage(QString("String '%1' not found").
                                   arg(m_searchSettings.m_originalText));
        }
        m_searchRequestId = 0;
    }
}

void DisasmWindow::symbolTableChangedSlot(uint64_t /*responseId*/)
{
    // This is for our autocomplete
    m_pSymbolTableModel->emitChanged();
}


void DisasmWindow::syncUiButtons()
{
    // Update UI to match
    m_pProcButton->setText(m_pDisasmWidget->GetProc() == kProcCpu ? "CPU" : "DSP");

    bool enabled = m_pTargetModel->IsDspActive();
    m_pProcButton->setEnabled(enabled);
}


void DisasmWindow::SetProc(Processor mode)
{
    // Don't do this pointlessly
    if (m_pDisasmWidget->GetProc() != mode)
        m_pDisasmWidget->SetProc(mode);
}

void DisasmWidget::Line::Reset()
{
    proc = kProcCpu;
    address = 0;
    inst56.reset();
    inst68.reset();
    memset(mem, 0, sizeof(mem));
    annotations.Reset();
}

uint32_t DisasmWidget::Line::GetByteSize() const
{
    if (proc == kProcCpu)
        return inst68.byte_count;
    else
        return inst56.word_count * 3;
}

uint32_t DisasmWidget::Line::GetEndAddr() const
{
    if (proc == kProcCpu)
        return address + inst68.byte_count;
    else
        return address + inst56.word_count;
}

void DisasmWidget::Line::Annotations::Reset()
{
    valid[0] = valid[1] = false;
    osComments.clear();
}
