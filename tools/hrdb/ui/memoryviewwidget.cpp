#include "memoryviewwidget.h"

#include <iostream>
#include <QGroupBox>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QStringListModel>
#include <QFontDatabase>
#include <QCompleter>
#include <QPainter>
#include <QKeyEvent>
#include <QSettings>
#include <QShortcut>
#include <QToolTip>
#include <QTextStream>

#include "../transport/dispatcher.h"
#include "../models/stringformat.h"
#include "../models/targetmodel.h"
#include "../models/stringparsers.h"
#include "../models/symboltablemodel.h"
#include "../models/session.h"
#include "colouring.h"
#include "elidedlabel.h"
#include "quicklayout.h"
#include "searchdialog.h"
#include "symboltext.h"

/*
 * Memory handling in the views
 *
 * Basic idea:
 *  Layout, base memory address and cursor position data is always updated immediately
 *  after a UI input.
 *  Memory is requested and a recalc of the text is done based on the new memory received.
 *  Text calculation is redone after UI changes or memory requests, but must handle memory
 *  being missing from the visible zone.
 */

static QString CreateTooltip(uint32_t address, const SymbolTable& symTable, uint8_t byteVal, uint32_t wordVal, uint32_t longVal)
{
    QString final;
    QTextStream ref(&final);

    ref << QString::asprintf("Address: $%x\n", address);
    Symbol sym;
    if (symTable.FindLowerOrEqual(address, true, sym))
    {
        QString symText = DescribeSymbol(symTable, address);
        ref << "Symbol: " << symText;

        QString comment = DescribeSymbolComment(symTable, address);
        if (!comment.isEmpty())
            ref << " (" << comment << ")";

        ref << "\n";
    }

    ref << QString::asprintf("\nLong: $%x -> %u", longVal, longVal);
    if (longVal & 0x80000000)
        ref << QString::asprintf(" (%d)", static_cast<int32_t>(longVal));

    ref << "\n   Bin: ";
    for (int bit = 31; bit >= 0; --bit)
    {
        ref << ((longVal & (1U << bit)) ? "1" : "0");
        if (bit && ((bit & 7) == 0))
            ref << "/";
    }

    ref << QString::asprintf("\nWord: $%x -> %u", wordVal, wordVal);
    if (wordVal & 0x8000)
        ref << QString::asprintf(" (%d)", static_cast<int16_t>(wordVal));

    ref << QString::asprintf("\nByte: $%x -> %u", byteVal, byteVal);
    if (byteVal & 0x80)
        ref << QString::asprintf(" (%d)", static_cast<int8_t>(byteVal));

    return final;
}

static int32_t GetDivider(MemSpace space)
{
    if (space != MEM_CPU)
        return 3;
    return 1;
}


MemoryWidget::MemoryWidget(QWidget *parent, Session* pSession,
                           int windowIndex,
                           QAction* pSearchAction, QAction* pSaveAction) :
    QWidget(parent),
    m_pSession(pSession),
    m_pTargetModel(pSession->m_pTargetModel),
    m_pDispatcher(pSession->m_pDispatcher),
    m_isLocked(false),
    m_address(maddr(MEM_CPU, 0)),
    m_widthMode(k16),
    m_sizeMode(kModeByte),
    m_bytesPerRow(16),
    m_hasAscii(true),
    m_rowCount(1),
    m_cursorRow(0),
    m_cursorCol(0),
    m_requestId(0),
    m_requestCursorMode(kNoMoveCursor),
    m_windowIndex(windowIndex),
    m_currentMemory(MEM_CPU, 0, 0),
    m_previousMemory(MEM_CPU, 0, 0),
    m_pSearchAction(pSearchAction),
    m_pSaveAction(pSaveAction),
    m_wheelAngleDelta(0)
{
    m_memSlot = static_cast<MemorySlot>(MemorySlot::kMemoryView0 + m_windowIndex);

    m_lastAddresses[MEM_CPU] = 0;
    m_lastAddresses[MEM_P] = 0;
    m_lastAddresses[MEM_X] = 0;
    m_lastAddresses[MEM_Y] = 0;

    UpdateFont();
    SetSizeMode(SizeMode::kModeByte);
    setFocus();
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(true);
    RecalcCursorInfo();

    connect(m_pTargetModel, &TargetModel::memoryChangedSignal,      this, &MemoryWidget::memoryChanged);
    connect(m_pTargetModel, &TargetModel::startStopChangedSignal,   this, &MemoryWidget::startStopChanged);
    connect(m_pTargetModel, &TargetModel::registersChangedSignal,   this, &MemoryWidget::registersChanged);
    connect(m_pTargetModel, &TargetModel::connectChangedSignal,     this, &MemoryWidget::connectChanged);
    connect(m_pTargetModel, &TargetModel::otherMemoryChangedSignal, this, &MemoryWidget::otherMemoryChanged);
    connect(m_pTargetModel, &TargetModel::symbolTableChangedSignal, this, &MemoryWidget::symbolTableChanged);
    connect(m_pTargetModel, &TargetModel::configChangedSignal,      this, &MemoryWidget::configChanged);
    connect(m_pSession,     &Session::settingsChanged,              this, &MemoryWidget::settingsChanged);
}

MemoryWidget::~MemoryWidget()
{
}

void MemoryWidget::SetSpace(MemSpace space)
{
    // If we use the combo, any locked expression is cancelled
    // and we revert to the last address known in this space
    SetAddressInternal(maddr(space, m_lastAddresses[space]));

    // Cancel any lock if we are switch between CPU<->DSP
    bool wasCpu = m_address.space == MEM_CPU;
    bool isCpu = space == MEM_CPU;
    if (m_isLocked && (wasCpu != isCpu))
        SetLock(false);     // this won't request memory since we are clearing the lock.

    RequestMemory(CursorMode::kNoMoveCursor); // fallback if no expression
}

void MemoryWidget::SetAddress(MemAddr full)
{
    // If we use the combo, any locked expression is cancelled
    // and we revert to the last address known in this space
    SetAddressInternal(full);

    if (m_isLocked)
        SetLock(false);

    RequestMemory(CursorMode::kNoMoveCursor);
}

// Set an expression. Could be locked, could include a space
// element e.g. "P:" at the start
bool MemoryWidget::SetExpression(std::string expression)
{
    // This does a "once only"
    uint32_t addr;
    bool res = ParseExpression(m_address.space == MEM_CPU, expression, addr);
    if (!res)
        return false;
    SetAddressInternal(maddr(m_address.space, addr));
    RequestMemory(kNoMoveCursor);
    m_addressExpression = expression;
    return true;
}

void MemoryWidget::SetAddressInternal(MemAddr addr)
{
    SetSpaceInternal(addr.space);
    m_address.addr = addr.addr;
    if (addr.space != MEM_CPU)
        m_address.addr &= 0xffff;       // Mask DSP addresses
}


bool MemoryWidget::CanSetExpression(std::string expression) const
{
    uint32_t addr;
    return ParseExpression(m_address.space == MEM_CPU, expression.c_str(), addr);
}


void MemoryWidget::SetSearchResultAddress(MemAddr addr)
{
    SetAddressInternal(addr);
    RequestMemory(kMoveCursor);
}


void MemoryWidget::SetRowCount(int32_t rowCount)
{
    // Handle awkward cases by always having at least one row
    if (rowCount < 1)
        rowCount = 1;
    if (rowCount != m_rowCount)
    {
        m_rowCount = rowCount;
        RequestMemory(kNoMoveCursor);

        if (m_cursorRow >= m_rowCount)
        {
            m_cursorRow = m_rowCount - 1;
        }
    }
}


void MemoryWidget::SetLock(bool locked)
{
    bool changed = (locked != m_isLocked);

    // Do this here so that RecalcLockedExpression works
    m_isLocked = locked;
    if (changed && locked)
    {
        // Lock has been turned on
        // Recalculate this expression for locking
        RecalcLockedExpression();
        RequestMemory(kNoMoveCursor);
    }
    // Don't clear the expression yet, since it might be reused
}


void MemoryWidget::SetSizeMode(MemoryWidget::SizeMode mode)
{
    // This doesn't change the amount of memory needed, so no need to re-request
    m_sizeMode = mode;
    RecalcColumnLayout();
    RecalcText();
    RecalcCursorInfo();
}

void MemoryWidget::SetWidthMode(WidthMode widthMode)
{
    // Store for UI
    m_widthMode = widthMode;

    if (m_address.space != MEM_CPU)
    {
        // DSP width is fixed -- or do we have a custom enum?
        m_hasAscii = false;
        m_bytesPerRow = 3 * 8;
    }
    else
    {
        m_hasAscii = true;
        switch (widthMode)
        {
        case WidthMode::k4:      m_bytesPerRow = 4; break;
        case WidthMode::k8:      m_bytesPerRow = 8; break;
        case WidthMode::k16:     m_bytesPerRow = 16; break;
        case WidthMode::k32:     m_bytesPerRow = 32; break;
        case WidthMode::k64:     m_bytesPerRow = 64; break;
        case WidthMode::kAuto:   RecalcAutoRowWidth(); break;
        default:                 break;
        }
    }

    RecalcColumnLayout();

    // Re-request memory
    RequestMemory(kNoMoveCursor);
}

void MemoryWidget::CursorUp()
{
    if (m_cursorRow > 0)
    {
        --m_cursorRow;
        update();
        RecalcCursorInfo();
        return;
    }
    MoveRelative(-m_bytesPerRow);
}

void MemoryWidget::CursorDown()
{
    if (m_cursorRow < m_rowCount - 1)
    {
        ++m_cursorRow;
        RecalcCursorInfo();
        update();
        return;
    }
    MoveRelative(+m_bytesPerRow);
}

void MemoryWidget::MoveLeft()
{
    if (m_cursorCol == 0)
        return;

    m_cursorCol--;
    RecalcCursorInfo();
    update();
}

void MemoryWidget::MoveRight()
{
    m_cursorCol++;
    if (m_cursorCol + 1 >= m_columnMap.size())
        m_cursorCol = m_columnMap.size() - 1;
    RecalcCursorInfo();
    update();
}

void MemoryWidget::PageUp()
{
    if (m_cursorRow > 0)
    {
        m_cursorRow = 0;
        RecalcCursorInfo();
        update();
        return;
    }
    MoveRelative(-m_bytesPerRow * m_rowCount);
}

void MemoryWidget::PageDown()
{
    if (m_cursorRow < m_rowCount - 1)
    {
        m_cursorRow = m_rowCount - 1;
        RecalcCursorInfo();
        update();
        return;
    }
    MoveRelative(+m_bytesPerRow * m_rowCount);
}

void MemoryWidget::MouseWheelUp()
{
    int numRows = std::max(m_rowCount / 4, 1);
    MoveRelative(-numRows * m_bytesPerRow);
}

void MemoryWidget::MouseWheelDown()
{
    int numRows = std::max(m_rowCount / 4, 1);
    MoveRelative(+numRows * m_bytesPerRow);
}

void MemoryWidget::MoveRelative(int32_t bytes)
{
    if (m_requestId != 0)
        return; // not up to date

    // "Jump" is the change in address, not bytes
    int32_t jump = bytes / GetDivider(m_address.space);

    if (jump >= 0)
    {
        uint32_t jumpAbs(static_cast<uint32_t>(jump));
        SetAddressInternal(maddr(m_address.space, m_address.addr + jumpAbs));
    }
    else
    {
        // "bytes" is negative, so convert to unsigned for calcs
        uint32_t jumpAbs(static_cast<uint32_t>(-jump));
        if (m_address.addr > jumpAbs)
            SetAddressInternal(maddr(m_address.space, m_address.addr - jumpAbs));
        else
            SetAddressInternal(maddr(m_address.space, 0));

    }
    RequestMemory(kNoMoveCursor);
}

bool MemoryWidget::EditKey(char key)
{
    // Can't edit while we still wait for memory
    if (m_requestId != 0)
        return false;

    // Can only edit CPU memory
    if (m_address.space != MEM_CPU)
        return false;

    const ColInfo& info = m_columnMap[m_cursorCol];
    if (info.type == ColumnType::kSpace)
    {
        MoveRight();
        return false;
    }
    if (info.type == ColumnType::kInvalid)
        return false;

    uint32_t address = CalcAddress(m_address, m_cursorRow, m_cursorCol);
    uint8_t cursorByte;
    if (!m_currentMemory.ReadCpuByte(address, cursorByte))
        return false;

    uint8_t val;
    if (info.type == ColumnType::kBottomNybble)
    {
        if (!StringParsers::ParseHexChar(key, val))
            return false;

        cursorByte &= 0xf0;
        cursorByte |= val;
    }
    else if (info.type == ColumnType::kTopNybble)
    {
        if (!StringParsers::ParseHexChar(key, val))
            return false;
        cursorByte &= 0x0f;
        cursorByte |= val << 4;
    }
    else if (info.type == ColumnType::kASCII)
    {
        cursorByte = static_cast<uint8_t>(key);
    }

    QVector<uint8_t> data(1);
    data[0] = cursorByte;
    QString cmd = QString::asprintf("memset $%x 1 %02x", address, cursorByte);
    m_pDispatcher->WriteMemory(address, data);

    // Replace the value so that editing still works
    RequestMemory(kNoMoveCursor);

    MoveRight();
    return true;
}

// Decide whether a given key event will be used for our input (e.g. for hex or ASCII)
// This is needed so we can successfully override shortcuts.
// This is a bit messy since it replicates a lot of logic in EditKey()
char MemoryWidget::IsEditKey(const QKeyEvent* event)
{
    // Try edit keys
    if (event->text().size() == 0)
        return 0;

    if (event->modifiers() & Qt::ControlModifier)
        return 0;
    if (event->modifiers() & Qt::AltModifier)
        return 0;

    QChar ch = event->text().at(0);
    signed char ascii = ch.toLatin1();

    // Don't check the memory request here.
    // Otherwise it will fall through and allow the "S"
    // shortcut to step... only when there is a pending access.

    const ColInfo& info = m_columnMap[m_cursorCol];
    if (info.type == ColumnType::kSpace)
        return 0;

    uint8_t val;
    if (info.type == ColumnType::kBottomNybble)
    {
        if (!StringParsers::ParseHexChar(ascii, val))
            return 0;
        return ascii;
    }
    else if (info.type == ColumnType::kTopNybble)
    {
        if (!StringParsers::ParseHexChar(ascii, val))
            return 0;
        return ascii;
    }
    else if (info.type == ColumnType::kASCII)
    {
        if (ascii >= 32)
            return ascii;
    }
    return 0;
}

void MemoryWidget::SetSpaceInternal(MemSpace space)
{
    if (space != m_address.space)
    {
        m_address.space = space;

        // Re-do width calculations
        SetWidthMode(m_widthMode);
        RecalcColumnLayout();

        emit spaceChangedSignal();
    }
}

void MemoryWidget::memoryChanged(int memorySlot, uint64_t commandId)
{
    if (memorySlot != m_memSlot)
        return;

    // ignore out-of-date requests
    if (commandId != m_requestId)
        return;

    m_rows.clear();
    const Memory* pMem = m_pTargetModel->GetMemory(m_memSlot);
    if (!pMem)
        return;

    if (pMem->GetMemAddr() != m_address)
        return;

    if (m_requestCursorMode == kMoveCursor)
    {
        m_cursorRow = 0;
        m_cursorCol = 0;
        RecalcCursorInfo();
    }

    m_currentMemory = *pMem;
    RecalcText();

    RecalcCursorInfo();
    m_requestId = 0;
    m_requestCursorMode = kNoMoveCursor;
}

void MemoryWidget::RecalcColumnLayout()
{
    // Calc the screen postions.
    // I need a screen position for each *character* on the grid (i.e. nybble)
    int groupSize = 0;
    if (m_address.space != MEM_CPU)
        groupSize = 3;
    else if (m_sizeMode == kModeByte)
        groupSize = 1;
    else if (m_sizeMode == kModeWord)
        groupSize = 2;
    else if (m_sizeMode == kModeLong)
        groupSize = 4;

    m_columnMap.clear();
    int byte = 0;
    while (byte + groupSize <= m_bytesPerRow)
    {
        ColInfo info;
        for (int subByte = 0; subByte < groupSize; ++subByte)
        {
            info.byteOffset = byte;
            info.type = ColumnType::kTopNybble;
            m_columnMap.push_back(info);
            info.type = ColumnType::kBottomNybble;
            m_columnMap.push_back(info);
            ++byte;
        }

        // Insert a space
        info.type = ColumnType::kSpace;
        m_columnMap.push_back(info);
    }

    // Add ASCII
    if (m_hasAscii)
    {
        for (int byte2 = 0; byte2 < m_bytesPerRow; ++byte2)
        {
            ColInfo info;
            info.type = ColumnType::kASCII;
            info.byteOffset = byte2;
            m_columnMap.push_back(info);
        }
    }

    // Stop crash when resizing and cursor is at end
    if (m_cursorCol >= m_columnMap.size())
        m_cursorCol = m_columnMap.size() - 1;

    // After this we must recalculate the text grid so everything is in sync
    RecalcText();
}


// This reads the current positional state, and updates the matrix
// of text/hex data in the the UI
void MemoryWidget::RecalcText()
{
    const SymbolTable& symTable = m_pTargetModel->GetSymbolTable();

    static char toHex[] = "0123456789abcdef";
    int32_t rowCount = m_rowCount;
    int32_t colCount = m_columnMap.size();
    if (m_rows.size() != rowCount)
        m_rows.resize(rowCount);

    for (int32_t r = 0; r < rowCount; ++r)
    {
        RowData& row = m_rows[r];
        row.m_text.resize(colCount);
        row.m_types.resize(colCount);
        row.m_byteChanged.resize(colCount);
        row.m_symbolId.resize(colCount);

        // Adjust memory offsets for DSP memory
        uint32_t rowAddress;
        uint32_t newRowOffset;         // offset in bytes to m_currentMemory base
        uint32_t oldRowOffset;         // offset in bytes to m_previousMemory base
        uint32_t addressDivider = GetDivider(m_address.space);

        rowAddress = CalcAddress(m_address, r, 0);
        newRowOffset = (rowAddress - m_currentMemory.GetAddress()) * addressDivider;
        oldRowOffset = (rowAddress - m_previousMemory.GetAddress()) * addressDivider;

        bool sameSpace = m_previousMemory.GetSpace() == m_currentMemory.GetSpace();

        for (int col = 0; col < colCount; ++col)
        {
            const ColInfo& info = m_columnMap[col];
            uint8_t byteVal = 0;

            char outChar = '?';
            ColumnType outType = ColumnType::kInvalid;
            uint32_t newByteOffset = newRowOffset + info.byteOffset;
            uint32_t charAddress = CalcAddress(m_address, r, col);

            bool hasAddress = newByteOffset < m_currentMemory.GetSize();
            bool changed = false;
            if (hasAddress)
            {
                byteVal = m_currentMemory.Get(newByteOffset);

                // Check previous memory version
                if (sameSpace)
                {
                    uint32_t oldByteOffset = oldRowOffset + info.byteOffset;
                    bool hadAddress = oldByteOffset < m_previousMemory.GetSize();
                    if (hasAddress && hadAddress)
                    {
                        uint8_t oldC = m_previousMemory.Get(oldByteOffset);
                        if (oldC != byteVal)
                            changed = true;
                    }
                }

                switch (info.type)
                {
                case ColumnType::kTopNybble:
                    if (hasAddress)
                    {
                        outChar = toHex[(byteVal >> 4) & 0xf];
                        outType = info.type;
                    }
                    break;
                case ColumnType::kBottomNybble:
                    if (hasAddress)
                    {
                        outChar = toHex[byteVal & 0xf];
                        outType = info.type;
                    }
                    break;
                case ColumnType::kASCII:
                    if (hasAddress)
                    {
                        outChar = '.';
                        if (byteVal >= 32 && byteVal < 128)
                            outChar = static_cast<char>(byteVal);
                        outType = info.type;
                    }
                    break;
                case ColumnType::kSpace:
                    outChar = ' ';
                    outType = info.type;
                    break;
                case ColumnType::kInvalid:
                    outChar = '?';
                    break;
                }
            }
            else
            {
                outChar = '?';
            }

            row.m_symbolId[col] = -1;
            if (m_address.space == MEM_CPU)
            {
                Symbol sym;
                if (info.type != ColumnType::kSpace)
                {
                    if (symTable.FindLowerOrEqual(charAddress & 0xffffff, true, sym))
                        row.m_symbolId[col] = (int)sym.index;
                }
            }
            row.m_text[col] = outChar;
            row.m_types[col] = outType;
            row.m_byteChanged[col] = changed;
        }
    }
    update();
}

void MemoryWidget::startStopChanged()
{
    // Request new memory for the view
    if (!m_pTargetModel->IsRunning())
    {
        // Normally we would request memory here, but it can be an expression.
        // So leave it to registers changing
    }
    else {
        // Starting to run
        // Copy the previous set of memory we have
        const Memory* pMem = m_pTargetModel->GetMemory(m_memSlot);
        if (pMem)
            m_previousMemory = *pMem;
        else {
            m_previousMemory = Memory(MEM_CPU, 0, 0);
        }
    }
}

void MemoryWidget::connectChanged()
{
    m_rows.clear();
    m_address.space = MEM_CPU;
    m_address.addr = 0;
    RecalcCursorInfo();
    update();
}

void MemoryWidget::registersChanged()
{
    // New registers can affect expression parsing
    RecalcLockedExpression();

    // Registers changing is our signal for re-request (rather than start/stop),
    // but do it after any locked expression has changed
    RequestMemory(CursorMode::kNoMoveCursor);
}

void MemoryWidget::otherMemoryChanged(uint32_t address, uint32_t size)
{
    // Do a re-request, only if it affected our view
    if (address + size <= m_address.addr)
        return;

    uint32_t ourSize = static_cast<uint32_t>(m_rowCount * m_bytesPerRow);
    if (m_address.addr + ourSize <= address)
        return;
    RequestMemory(kNoMoveCursor);
}

void MemoryWidget::symbolTableChanged()
{
    // New symbol table can affect expression parsing
    RecalcLockedExpression();
    RequestMemory(kNoMoveCursor);
}

void MemoryWidget::settingsChanged()
{
    UpdateFont();
    RecalcRowCount();
    RequestMemory(kNoMoveCursor);
    update();
}

void MemoryWidget::configChanged()
{
    // Force back to CPU if there's no DSP
    if (!m_pTargetModel->IsDspActive())
        SetSpace(MEM_CPU);
}

void MemoryWidget::paintEvent(QPaintEvent* ev)
{
    QWidget::paintEvent(ev);
    QPainter painter(this);
    const QPalette& pal = this->palette();

    if (m_pTargetModel->IsConnected() && m_rows.size() != 0)
    {
        painter.setFont(m_monoFont);
        QFontMetrics info(painter.fontMetrics());

        // Compensate for text descenders
        int y_ascent = info.ascent();
        int char_width = info.horizontalAdvance("0");

        // Set up the rendering info
        QColor backCol = pal.window().color();
        QColor cols[7] =
        {
            QColor(backCol.red() ^  0, backCol.green() ^ 32, backCol.blue() ^ 0),
            QColor(backCol.red() ^ 32, backCol.green() ^  0, backCol.blue() ^ 0),
            QColor(backCol.red() ^  0, backCol.green() ^ 32, backCol.blue() ^ 32),
            QColor(backCol.red() ^ 32, backCol.green() ^  0, backCol.blue() ^ 32),
            QColor(backCol.red() ^  0, backCol.green() ^  0, backCol.blue() ^ 32),
            QColor(backCol.red() ^ 32, backCol.green() ^ 32, backCol.blue() ^ 0),
            QColor(backCol.red() ^ 32, backCol.green() ^ 32, backCol.blue() ^ 32),
        };

        painter.setPen(pal.text().color());
        for (int row = 0; row < m_rows.size(); ++row)
        {
            const RowData& r = m_rows[row];

            // Draw address string
            painter.setPen(pal.text().color());
            int topleft_y = GetPixelFromRow(row);
            int text_y = topleft_y + y_ascent;
            uint32_t rowAddr = CalcAddress(m_address, row, 0);

            QString addr;
            switch (m_address.space)
            {
            case MEM_CPU: addr = QString::asprintf("%08x", rowAddr); break;
            case MEM_P: addr = QString::asprintf("P:$%04x", rowAddr); break;
            case MEM_X: addr = QString::asprintf("X:$%04x", rowAddr); break;
            case MEM_Y: addr = QString::asprintf("Y:$%04x", rowAddr); break;
            default: break;
            }
            painter.drawText(GetAddrX(), text_y, addr);

            // Now hex
            // We write out the values per-nybble
            for (int col = 0; col < m_columnMap.size(); ++col)
            {
                int x = GetPixelFromCol(col);
                // Mark symbol
                if (r.m_symbolId[col] != -1)
                {
                    painter.setBrush(cols[r.m_symbolId[col] % 7]);
                    painter.setPen(Qt::NoPen);
                    painter.drawRect(x, topleft_y, m_charWidth, m_lineHeight);
                }

                bool changed = r.m_byteChanged[col];
                QChar st = r.m_text.at(col);
                painter.setPen(changed ? Qt::red : pal.text().color());
                painter.drawText(x, text_y, st);
            }
        }

        // Draw highlight/cursor area in the hex
        QBrush cursorColour = hasFocus() ? pal.highlight() : pal.mid();
        if (m_cursorRow >= 0 && m_cursorRow < m_rows.size())
        {
            int y_curs = GetPixelFromRow(m_cursorRow);
            int x_curs = GetPixelFromCol(m_cursorCol);

            painter.setBrush(cursorColour);
            painter.drawRect(x_curs, y_curs, char_width, m_lineHeight);

            QChar st = m_rows[m_cursorRow].m_text.at(m_cursorCol);
            painter.setPen(pal.highlightedText().color());
            painter.drawText(x_curs, y_ascent + y_curs, st);
        }
    }

    // Draw border last
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(pal.dark(), hasFocus() ? 6 : 2));
    painter.drawRect(this->rect());
}

void MemoryWidget::keyPressEvent(QKeyEvent* event)
{
    if (m_pTargetModel->IsConnected())
    {
        if (event->modifiers() == Qt::NoModifier)
        {
            switch (event->key())
            {
            case Qt::Key_Left:       MoveLeft();          return;
            case Qt::Key_Right:      MoveRight();         return;
            case Qt::Key_Up:         CursorUp();          return;
            case Qt::Key_Down:       CursorDown();        return;
            case Qt::Key_PageUp:     PageUp();            return;
            case Qt::Key_PageDown:   PageDown();          return;
            default: break;
            }
        }
        else if (event->modifiers() == Qt::ControlModifier)
        {
            switch (event->key())
            {
            case Qt::Key_Space:      KeyboardContextMenu(); return;
            default: break;
            }
        }

        // All keys, with or without shift
        char key = IsEditKey(event);
        if (key)
        {
            // We think a key should be acted upon.
            // Try the work, and exit even if it failed,
            // to block any accidental Shortcut trigger.
            EditKey(key);
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void MemoryWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_pTargetModel->IsConnected())
    {
        if (event->button() == Qt::MouseButton::LeftButton)
        {
            // Over a hex char
            int x = static_cast<int>(event->localPos().x());
            int y = static_cast<int>(event->localPos().y());
            int row;
            int col;
            if (CalcRowColFromMouse(x, y, row, col))
            {
                m_cursorCol = col;
                m_cursorRow = row;
                RecalcCursorInfo();
                update();
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void MemoryWidget::wheelEvent(QWheelEvent *event)
{
    if (!this->underMouse() || !m_pTargetModel->IsConnected())
    {
        event->ignore();
        return;
    }

    // Accumulate delta for some mice
    m_wheelAngleDelta += event->angleDelta().y();
    if (m_wheelAngleDelta >= 15)
    {
        MouseWheelUp();
        m_wheelAngleDelta = 0;
    }
    else if (m_wheelAngleDelta <= -15)
    {
        MouseWheelDown();
        m_wheelAngleDelta = 0;
    }
    event->accept();
}

void MemoryWidget::contextMenuEvent(QContextMenuEvent *event)
{
    // Over a hex char
    int x = static_cast<int>(event->pos().x());
    int y = static_cast<int>(event->pos().y());
    int row;
    int col;
    if (!CalcRowColFromMouse(x, y, row, col))
    {
         ContextMenu(-1, -1, event->globalPos());
         return;
    }

    if (m_columnMap[col].type == ColumnType::kSpace)
    {
        ContextMenu(-1, -1, event->globalPos());
        return;
    }

    ContextMenu(row, col, event->globalPos());
}

void MemoryWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    RecalcRowCount();
    if (m_widthMode == kAuto)
    {
        RecalcAutoRowWidth();
        RecalcText();
        RequestMemory(kNoMoveCursor);
    }
}

bool MemoryWidget::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {

        QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);
        QString text = "";

        int x = static_cast<int>(helpEvent->pos().x());
        int y = static_cast<int>(helpEvent->pos().y());

        text = CalcMouseoverText(x, y);
        if (!text.isNull())
            QToolTip::showText(helpEvent->globalPos(), text);
        else
        {
            QToolTip::hideText();
            event->ignore();
        }
        return true;
    }
    else if (event->type() == QEvent::ShortcutOverride) {
        // If there is a usable key, allow this rather than passing through to global
        // shortcut
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        char key = IsEditKey(ke);
        if (key)
           event->accept();
    }
    return QWidget::event(event);
}

void MemoryWidget::RequestMemory(MemoryWidget::CursorMode moveCursor)
{
    // Memory size requested is based on the current matrix dimensions
    uint32_t size = static_cast<uint32_t>(m_rowCount * m_bytesPerRow);

    if (m_pTargetModel->IsConnected())
    {
        // Adjust for DSP words...
        if (m_address.space != MEM_CPU)
            size = ((size + 2U) / 3U);

        m_requestId = m_pDispatcher->ReadMemory(m_memSlot, m_address.space, m_address.addr, size);
        m_requestCursorMode = moveCursor;
    }
    // Record the last-requested address for this space.
    m_lastAddresses[m_address.space] = m_address.addr;
}

void MemoryWidget::RecalcLockedExpression()
{
    if (m_isLocked)
    {
        uint32_t addr;
        if (ParseExpression(m_address.space == MEM_CPU,
                            m_addressExpression, addr))
        {
            SetAddressInternal(maddr(m_address.space, addr));
        }
    }
}

bool MemoryWidget::ParseExpression(bool isCpu, const std::string& expr, uint32_t& addr) const
{
    if (isCpu)
        return StringParsers::ParseCpuExpression(expr.c_str(), addr,
                                            m_pTargetModel->GetSymbolTable(),
                                            m_pTargetModel->GetRegs());
    else
        return StringParsers::ParseDspExpression(expr.c_str(), addr,
                                            m_pTargetModel->GetSymbolTable(),
                                            m_pTargetModel->GetDspRegs());
}

void MemoryWidget::RecalcRowCount()
{
    int h = this->size().height() - (Session::kWidgetBorderY * 2);
    int rowh = m_lineHeight;
    int count = 0;
    if (rowh != 0)
        count = h / rowh;
    SetRowCount(count);

    if (m_cursorRow >= m_rowCount)
    {
        m_cursorRow = m_rowCount - 1;
        RecalcCursorInfo();
    }
    // After this we must recalc the text so everything is in sync
    RecalcText();
}

void MemoryWidget::RecalcAutoRowWidth()
{   
    int w = this->size().width() - (Session::kWidgetBorderX * 2);

    // calc how many chars fit
    int charWidth = m_charWidth;
    int numChars = w / charWidth;

    // 10 chars are used for address + border
    numChars -= 10;
    int finalChunkCount = 1;
    switch (m_sizeMode)
    {
    case SizeMode::kModeByte:
        finalChunkCount = numChars / (12 + 4);   // xx xx xx xx   4 lots of 3, plus 4 ascii
        break;
    case SizeMode::kModeWord:
        finalChunkCount = numChars / (10 + 4);   // xxxx xxxx     2 lots of 5
        break;
    case SizeMode::kModeLong:
        finalChunkCount = numChars / (9 + 4);    // xxxxxxxx      1 lot of 9
        break;
    }

    if (finalChunkCount < 1)
        finalChunkCount = 1;

    if (m_bytesPerRow != finalChunkCount * 4)
    {
        m_bytesPerRow = finalChunkCount * 4;
        RecalcColumnLayout();
    }
}

void MemoryWidget::RecalcCursorInfo()
{
    m_cursorInfo.m_cursorAddress = 0;
    m_cursorInfo.m_isCursorValid = false;
    m_cursorInfo.m_startAddress = CalcAddress(m_address, 0, 0);
    m_cursorInfo.m_sizeInBytes = m_rowCount * m_bytesPerRow;

    if (m_cursorRow < m_rows.size())
    {
        // Search leftwards for the first valid character
        for (int col = m_cursorCol; col >= 0; --col)
        {
            const ColInfo& info = m_columnMap[col];
            if (info.type == ColumnType::kSpace)
                break;
            m_cursorInfo.m_cursorAddress = CalcAddress(m_address, m_cursorRow, m_cursorCol);
            m_cursorInfo.m_isCursorValid = true;
            break;
        }
    }
    emit cursorChangedSignal();
}

QString MemoryWidget::CalcMouseoverText(int mouseX, int mouseY)
{
    int row;
    int col;
    if (!CalcRowColFromMouse(mouseX, mouseY, row, col))
        return QString();

    if (m_columnMap[col].type == ColumnType::kSpace)
        return QString();

    const Memory* mem = m_pTargetModel->GetMemory(m_memSlot);
    if (!mem)
        return QString();

    if (mem->GetMemAddr().space != MEM_CPU)
        return QString("DSP");

    uint32_t addr = CalcAddress(m_address, row, col);
    uint8_t byteVal;
    mem->ReadCpuByte(addr, byteVal);

    uint32_t wordVal;
    if (!mem->ReadCpuMulti(addr & 0xfffffe, 2, wordVal))
        return QString();

    // Work out what's under the mouse

    // Align the memory location to 2 or 4 bytes, based on context
    // (view address, or word/long mode)
    uint32_t addrLong = addr;

    if (m_sizeMode == SizeMode::kModeLong)
        addrLong &= ~3U;        // $fc
    else
        addrLong &= ~1U;        // $fe

    uint32_t longVal;
    if (!mem->ReadCpuMulti(addrLong, 4, longVal))
        return QString();
    return CreateTooltip(addr, m_pTargetModel->GetSymbolTable(), byteVal, wordVal, longVal);
}

void MemoryWidget::UpdateFont()
{
    m_monoFont = m_pSession->GetSettings().m_font;
    QFontMetrics info(m_monoFont);
    m_lineHeight = info.lineSpacing();
    m_charWidth = info.horizontalAdvance("0");
}

// Position in pixels of address column
int MemoryWidget::GetAddrX() const
{
    return Session::kWidgetBorderX;
}

int MemoryWidget::GetPixelFromRow(int row) const
{
    return Session::kWidgetBorderY + row * m_lineHeight;
}

int MemoryWidget::GetRowFromPixel(int y) const
{
    if (!m_lineHeight)
        return 0;
    return (y - Session::kWidgetBorderY) / m_lineHeight;
}

void MemoryWidget::KeyboardContextMenu()
{
    ContextMenu(m_cursorRow, m_cursorCol,
                mapToGlobal(QPoint(GetPixelFromCol(m_cursorCol), GetPixelFromRow(m_cursorRow))));
}

void MemoryWidget::ContextMenu(int row, int col, QPoint globalPos)
{
    QMenu menu(this);
    MemSpace space = m_address.space;
    bool isCpu = (space == MEM_CPU);

    if (isCpu)
        menu.addAction(m_pSaveAction);

    // These actions are optional
    if (row != -1)
    {
        // Align the memory location to 2 or 4 bytes, based on context
        // (view address, or word/long mode)
        uint32_t addr = CalcAddress(m_address, row, col);
        if (isCpu)
        {
            if (m_sizeMode == SizeMode::kModeLong)
                addr &= ~3U;
            else
                addr &= ~1U;

            menu.addAction(m_pSearchAction);
        }

        // TODO different memory spaces
        m_showThisAddressMenu.Set("Cursor Address",
                                   m_pSession, space, addr);
        m_showThisAddressMenu.AddTo(&menu);

        const Memory* mem = m_pTargetModel->GetMemory(m_memSlot);
        if (mem)
        {
            bool valid = false;
            uint32_t contents;
            if (isCpu)
            {
                valid = mem->ReadCpuMulti(addr, 4, contents);
                if (valid)
                {
                    contents &= 0xffffff;
                    m_showPtrAddressMenu[0].Set("Data Address",
                                                m_pSession, m_address.space, contents);
                    m_showPtrAddressMenu[0].AddTo(&menu);
                }
            }
            else
            {
                valid = mem->ReadDspWord(addr, contents);
                if (valid)
                {
                    contents &= 0xffff;
                    // We don't know which memory space we want, so show all three.
                    m_showPtrAddressMenu[0].Set("Data Address", m_pSession, MEM_P, contents);
                    m_showPtrAddressMenu[1].Set("Pointer Address", m_pSession, MEM_X, contents);
                    m_showPtrAddressMenu[2].Set("Pointer Address", m_pSession, MEM_Y, contents);
                    m_showPtrAddressMenu[0].AddTo(&menu);
                    m_showPtrAddressMenu[1].AddTo(&menu);
                    m_showPtrAddressMenu[2].AddTo(&menu);
                }
            }

            if (valid)
            {
            }
        }
    }
    // Run it
    menu.exec(globalPos);
}

int MemoryWidget::GetPixelFromCol(int column) const
{
    return GetAddrX() + (10 + column) * m_charWidth;
}

bool MemoryWidget::CalcRowColFromMouse(int x, int y, int& row, int& col)
{
    row = GetRowFromPixel(y);
    if (row >= 0 && row < m_rows.size())
    {
        // Find the X char that might fit
        for (col = 0; col < m_columnMap.size(); ++col)
        {
            int charPos = GetPixelFromCol(col);
            if (x >= charPos && x < charPos + m_charWidth)
                return true;
        }
    }
    return false;
}

uint32_t MemoryWidget::CalcAddrOffset(MemAddr addr, int row, int col) const
{
    uint32_t divider = addr.space == MEM_CPU ? 1 : 3;
    const ColInfo& info = m_columnMap[col];
    uint32_t charByteOffset = row * m_bytesPerRow + info.byteOffset;
    return (charByteOffset / divider);
}

uint32_t MemoryWidget::CalcAddress(MemAddr addr, int row, int col) const
{
    return addr.addr + CalcAddrOffset(addr, row, col);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
MemoryWindow::MemoryWindow(QWidget *parent, Session* pSession, int windowIndex) :
    QDockWidget(parent),
    m_pSession(pSession),
    m_pTargetModel(pSession->m_pTargetModel),
    m_pDispatcher(pSession->m_pDispatcher),
    m_windowIndex(windowIndex),
    m_searchRequestId(0)
{
    this->setWindowTitle(QString::asprintf("Memory %d", windowIndex + 1));
    QString key = QString::asprintf("MemoryView%d", m_windowIndex);
    setObjectName(key);

    // Search action created first so we can pass it to the child widget
    QAction* pMenuSearchAction = new QAction("Search...", this);
    pMenuSearchAction->setShortcut(QKeySequence("Ctrl+F"));
    connect(pMenuSearchAction, &QAction::triggered, this, &MemoryWindow::findClickedSlot);
    QAction* pMenuSaveAction = new QAction("Write to File...", this);
    pMenuSaveAction->setShortcut(QKeySequence("Ctrl+W"));
    connect(pMenuSaveAction, &QAction::triggered, this, &MemoryWindow::saveBinClickedSlot);

    m_pSymbolTableModel = new SymbolTableModel(this, m_pTargetModel->GetSymbolTable());
    QCompleter* pCompl = new QCompleter(m_pSymbolTableModel, this);
    pCompl->setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);

    // Construction. Do in order of tabbing
    m_pMemoryWidget = new MemoryWidget(this, pSession, windowIndex, pMenuSearchAction, pMenuSaveAction);
    m_pMemoryWidget->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));

    m_pAddressEdit = new QLineEdit(this);
    m_pAddressEdit->setCompleter(pCompl);

    m_pSpaceComboBox = new QComboBox(this);
    m_pSpaceComboBox->insertItem(0, "CPU", QVariant(MEM_CPU));
    m_pSpaceComboBox->insertItem(1, "P:", QVariant(MEM_P));
    m_pSpaceComboBox->insertItem(2, "X:", QVariant(MEM_X));
    m_pSpaceComboBox->insertItem(3, "Y:", QVariant(MEM_Y));

    m_pLockCheckBox = new QCheckBox(tr("Lock"), this);
    m_pCursorInfoLabel = new ElidedLabel("", this);

    m_pSizeModeComboBox = new QComboBox(this);
    m_pSizeModeComboBox->insertItem(MemoryWidget::kModeByte, "Byte");
    m_pSizeModeComboBox->insertItem(MemoryWidget::kModeWord, "Word");
    m_pSizeModeComboBox->insertItem(MemoryWidget::kModeLong, "Long");
    m_pSizeModeComboBox->setCurrentIndex(m_pMemoryWidget->GetSizeMode());

    m_pWidthComboBox = new QComboBox(this);
    m_pWidthComboBox->insertItem(0, "4 bytes", QVariant(MemoryWidget::k4));
    m_pWidthComboBox->insertItem(1, "8 bytes", QVariant(MemoryWidget::k8));
    m_pWidthComboBox->insertItem(2, "16 bytes", QVariant(MemoryWidget::k16));
    m_pWidthComboBox->insertItem(3, "32 bytes", QVariant(MemoryWidget::k32));
    m_pWidthComboBox->insertItem(4, "64 bytes", QVariant(MemoryWidget::k64));
    m_pWidthComboBox->insertItem(5, "Auto", QVariant(MemoryWidget::kAuto));
    m_pWidthComboBox->setCurrentIndex(m_pMemoryWidget->GetWidthMode());

    // Layouts
    QVBoxLayout* pMainLayout = new QVBoxLayout;
    QHBoxLayout* pTopLayout = new QHBoxLayout;
    auto pMainRegion = new QWidget(this);   // whole panel
    auto pTopRegion = new QWidget(this);      // top buttons/edits

    SetMargins(pTopLayout);

    pTopLayout->addWidget(m_pSpaceComboBox);
    pTopLayout->addWidget(m_pAddressEdit);
    pTopLayout->addWidget(m_pLockCheckBox);
    pTopLayout->addWidget(m_pSizeModeComboBox);
    pTopLayout->addWidget(m_pWidthComboBox);

    SetMargins(pMainLayout);
    pMainLayout->addWidget(pTopRegion);
    pMainLayout->addWidget(m_pMemoryWidget);
    pMainLayout->addWidget(m_pCursorInfoLabel);

    pTopRegion->setLayout(pTopLayout);
    pMainRegion->setLayout(pMainLayout);
    setWidget(pMainRegion);

    loadSettings();

    // Ensure UI elements are up to date
    syncUiElements();

    // The scope here is explained at https://forum.qt.io/topic/67981/qshortcut-multiple-widget-instances/2
    new QShortcut(QKeySequence("Ctrl+F"),         this, SLOT(findClickedSlot()),    nullptr, Qt::WidgetWithChildrenShortcut);
    new QShortcut(QKeySequence("Ctrl+W"),         this, SLOT(saveBinClickedSlot()), nullptr, Qt::WidgetWithChildrenShortcut);
    new QShortcut(QKeySequence("F3"),             this, SLOT(nextClickedSlot()),    nullptr, Qt::WidgetWithChildrenShortcut);
    new QShortcut(QKeySequence("Ctrl+G"),         this, SLOT(gotoClickedSlot()),    nullptr, Qt::WidgetWithChildrenShortcut);
    new QShortcut(QKeySequence("Ctrl+L"),         this, SLOT(lockClickedSlot()),    nullptr, Qt::WidgetWithChildrenShortcut);

    connect(m_pAddressEdit,  &QLineEdit::returnPressed,                this, &MemoryWindow::returnPressedSlot);
    connect(m_pAddressEdit,  &QLineEdit::textChanged,                  this, &MemoryWindow::textEditedSlot);
    connect(m_pLockCheckBox, &QCheckBox::stateChanged,                 this, &MemoryWindow::lockClickedSlot);
    connect(m_pSession,      &Session::addressRequested,               this, &MemoryWindow::requestAddress);
    connect(m_pMemoryWidget, &MemoryWidget::cursorChangedSignal,       this, &MemoryWindow::cursorChangedSlot);
    connect(m_pMemoryWidget, &MemoryWidget::spaceChangedSignal,        this, &MemoryWindow::syncUiElements);
    connect(m_pMemoryWidget, &MemoryWidget::lockChangedSignal,         this, &MemoryWindow::syncUiElements);
    connect(m_pTargetModel,  &TargetModel::searchResultsChangedSignal, this, &MemoryWindow::searchResultsSlot);
    connect(m_pTargetModel,  &TargetModel::symbolTableChangedSignal,   this, &MemoryWindow::symbolTableChangedSlot);
    connect(m_pTargetModel,  &TargetModel::configChangedSignal,        this, &MemoryWindow::syncUiElements);

    connect(m_pSpaceComboBox,        SIGNAL(currentIndexChanged(int)), SLOT(spaceComboBoxChangedSlot(int)));
    connect(m_pSizeModeComboBox,     SIGNAL(currentIndexChanged(int)), SLOT(sizeModeComboBoxChangedSlot(int)));
    connect(m_pWidthComboBox,        SIGNAL(currentIndexChanged(int)), SLOT(widthComboBoxChangedSlot(int)));
}

void MemoryWindow::keyFocus()
{
    activateWindow();
    m_pMemoryWidget->setFocus();
}

void MemoryWindow::loadSettings()
{
    QSettings settings;
    QString key = QString::asprintf("MemoryView%d", m_windowIndex);
    settings.beginGroup(key);

    restoreGeometry(settings.value("geometry").toByteArray());

    int mode = settings.value("mode", QVariant(MemoryWidget::kModeByte)).toInt();
    m_pMemoryWidget->SetSizeMode(static_cast<MemoryWidget::SizeMode>(mode));
    // Sync UI
    m_pSizeModeComboBox->setCurrentIndex(m_pMemoryWidget->GetSizeMode());

    QVariant widthMode = settings.value("widthMode", QVariant(MemoryWidget::k16));
    int widthIndex = m_pWidthComboBox->findData(widthMode);
    if (widthIndex != -1)
        m_pWidthComboBox->setCurrentIndex(widthIndex);
    m_pMemoryWidget->SetWidthMode(static_cast<MemoryWidget::WidthMode>(widthMode.toInt()));

    settings.endGroup();
}

void MemoryWindow::saveSettings()
{
    QSettings settings;
    QString key = QString::asprintf("MemoryView%d", m_windowIndex);
    settings.beginGroup(key);

    settings.setValue("geometry", saveGeometry());
    settings.setValue("mode", static_cast<int>(m_pMemoryWidget->GetSizeMode()));
    settings.setValue("widthMode", static_cast<int>(m_pMemoryWidget->GetWidthMode()));
    settings.endGroup();
}

void MemoryWindow::requestAddress(Session::WindowType type, int windowIndex, int memorySpace, uint32_t address)
{
    if (type != Session::WindowType::kMemoryWindow)
        return;

    if (windowIndex != m_windowIndex)
        return;

    m_pMemoryWidget->SetSpace(static_cast<MemSpace>(memorySpace));
    m_pMemoryWidget->SetExpression(std::to_string(address));

    setVisible(true);
    this->keyFocus();
    raise();
}

void MemoryWindow::cursorChangedSlot()
{
    const MemoryWidget::CursorInfo& info = m_pMemoryWidget->GetCursorInfo();

    if (info.m_isCursorValid)
    {
        QString final;
        QTextStream ref(&final);
        ref << QString("Cursor: ") << Format::to_hex32(info.m_cursorAddress & 0xffffff);

        if (m_pMemoryWidget->GetSpace() == MEM_CPU)
        {
            QString symText = DescribeSymbol(m_pTargetModel->GetSymbolTable(), info.m_cursorAddress & 0xffffff);
            if (!symText.isEmpty())
                ref << " (" + symText + ")";

            QString commentText = DescribeSymbolComment(m_pTargetModel->GetSymbolTable(), info.m_cursorAddress);
            if (!commentText.isEmpty())
                ref << " " + commentText;
        }

        m_pCursorInfoLabel->setText(final);
    }
    else
        m_pCursorInfoLabel->setText("");
}

void MemoryWindow::returnPressedSlot()
{
    bool valid = m_pMemoryWidget->SetExpression(m_pAddressEdit->text().toStdString());
    Colouring::SetErrorState(m_pAddressEdit, valid);
    m_pLockCheckBox->setChecked(m_pMemoryWidget->IsLocked());
    if (valid)
        m_pMemoryWidget->setFocus();
}

void MemoryWindow::textEditedSlot()
{
    bool valid = m_pMemoryWidget->CanSetExpression(m_pAddressEdit->text().toStdString());
    Colouring::SetErrorState(m_pAddressEdit, valid);
}

void MemoryWindow::lockChangedSlot()
{
    m_pMemoryWidget->SetLock(m_pLockCheckBox->isChecked());
}

void MemoryWindow::spaceComboBoxChangedSlot(int /*index*/)
{
    int space = m_pSpaceComboBox->currentData().toInt();
    m_pMemoryWidget->SetSpace(static_cast<MemSpace>(space));
    m_pLockCheckBox->setChecked(m_pMemoryWidget->IsLocked());
}

void MemoryWindow::sizeModeComboBoxChangedSlot(int index)
{
    m_pMemoryWidget->SetSizeMode((MemoryWidget::SizeMode)index);
}

void MemoryWindow::widthComboBoxChangedSlot(int /*index*/)
{
    int widthMode = m_pWidthComboBox->currentData().toInt();
    m_pMemoryWidget->SetWidthMode((MemoryWidget::WidthMode)widthMode);
}

void MemoryWindow::findClickedSlot()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_pMemoryWidget->GetSpace() != MEM_CPU)
        return;

    const MemoryWidget::CursorInfo& info = m_pMemoryWidget->GetCursorInfo();
    if (!info.m_isCursorValid)
        return;

    // Fill in the "default"
    m_searchSettings.m_startAddress = info.m_cursorAddress;
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

void MemoryWindow::nextClickedSlot()
{
    if (!m_pTargetModel->IsConnected())
        return;

    if (m_pMemoryWidget->GetSpace() != MEM_CPU)
        return;

    const MemoryWidget::CursorInfo& info = m_pMemoryWidget->GetCursorInfo();
    if (!info.m_isCursorValid)
        return;

    if (m_searchSettings.m_masksAndValues.size() != 0)
    {
        // Start address should already have been filled
        {
            m_searchRequestId = m_pDispatcher->SendMemFind(m_searchSettings.m_masksAndValues,
                                     info.m_cursorAddress + 1,
                                     m_searchSettings.m_endAddress);
        }
    }
}

void MemoryWindow::saveBinClickedSlot()
{
    if (!m_pTargetModel->IsConnected())
        return;

    const MemoryWidget::CursorInfo& info = m_pMemoryWidget->GetCursorInfo();

    // Fill in the "default"
    m_saveBinSettings.m_startAddress = info.m_startAddress;
    m_saveBinSettings.m_sizeInBytes = info.m_sizeInBytes;

    SaveBinDialog search(this, m_pTargetModel, m_saveBinSettings);
    search.setModal(true);
    int code = search.exec();
    if (code == QDialog::DialogCode::Accepted &&
        m_pTargetModel->IsConnected())
    {
        std::string filename = m_saveBinSettings.m_filename.toStdString();
        (void) m_pDispatcher->SendSaveBin(m_saveBinSettings.m_startAddress,
                                          m_saveBinSettings.m_sizeInBytes,
                                          filename);
    }
}

void MemoryWindow::gotoClickedSlot()
{
    m_pAddressEdit->setFocus();
}

void MemoryWindow::lockClickedSlot()
{
    m_pMemoryWidget->SetLock(m_pLockCheckBox->isChecked());
}

void MemoryWindow::searchResultsSlot(uint64_t responseId)
{
    if (responseId == m_searchRequestId)
    {
        const SearchResults& results = m_pTargetModel->GetSearchResults();
        if (results.addresses.size() > 0)
        {
            uint32_t addr = results.addresses[0];
            m_pMemoryWidget->SetLock(false);
            m_pMemoryWidget->SetSearchResultAddress(maddr(MEM_CPU, addr));
            m_pLockCheckBox->setChecked(m_pMemoryWidget->IsLocked());

            // Allow the "next" operation to work
            m_searchSettings.m_startAddress = addr + 1;
            m_pMemoryWidget->setFocus();
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

void MemoryWindow::symbolTableChangedSlot(uint64_t /*responseId*/)
{
    m_pSymbolTableModel->emitChanged();
}

void MemoryWindow::syncUiElements()
{
    bool allowSpaceSetting = m_pTargetModel->IsDspActive();
    m_pSpaceComboBox->setEnabled(allowSpaceSetting);

    MemSpace space = m_pMemoryWidget->GetSpace();
    m_pWidthComboBox->setEnabled(space == MEM_CPU);
    m_pSizeModeComboBox->setEnabled(space == MEM_CPU);

    m_pLockCheckBox->setChecked(m_pMemoryWidget->IsLocked());

    // Update combo box UI from the new address
    m_pLockCheckBox->setChecked(m_pMemoryWidget->IsLocked());
    int spaceComboIndex = m_pWidthComboBox->findData((int)space);
    if (spaceComboIndex != -1)
        m_pSpaceComboBox->setCurrentIndex(spaceComboIndex);
}
