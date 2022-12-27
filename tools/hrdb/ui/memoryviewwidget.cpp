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
#include "quicklayout.h"
#include "searchdialog.h"

static QString CreateTooltip(uint32_t address, const SymbolTable& symTable, uint8_t byteVal, uint32_t wordVal, uint32_t longVal)
{
    QString final;
    QTextStream ref(&final);

    ref << QString::asprintf("Address: $%x\n", address);
    Symbol sym;
    if (symTable.FindLowerOrEqual(address, sym))
    {
        ref << QString::asprintf("Symbol: $%x %s", sym.address, sym.name.c_str());
        if (sym.address != address)
            ref << QString::asprintf("+$%x", address - sym.address);
    }

    ref << QString::asprintf("\nLong: $%x -> %u", longVal, longVal);
    if (longVal & 0x80000000)
        ref << QString::asprintf(" (%d)", static_cast<int32_t>(longVal));

    ref << QString::asprintf("\nWord: $%x -> %u", wordVal, wordVal);
    if (wordVal & 0x8000)
        ref << QString::asprintf(" (%d)", static_cast<int16_t>(wordVal));

    ref << QString::asprintf("\nByte: $%x -> %u", byteVal, byteVal);
    if (byteVal & 0x80)
        ref << QString::asprintf(" (%d)", static_cast<int8_t>(byteVal));

    return final;
}

MemoryWidget::MemoryWidget(QWidget *parent, Session* pSession,
                                           int windowIndex) :
    QWidget(parent),
    m_pSession(pSession),
    m_pTargetModel(pSession->m_pTargetModel),
    m_pDispatcher(pSession->m_pDispatcher),
    m_isLocked(false),
    m_bytesPerRow(16),
    m_mode(kModeByte),
    m_rowCount(1),
    m_address(0),
    m_requestId(0),
    m_requestCursorMode(kNoMoveCursor),
    m_windowIndex(windowIndex),
    m_previousMemory(0, 0),
    m_cursorRow(0),
    m_cursorCol(0),
    m_wheelAngleDelta(0)
{
    m_memSlot = static_cast<MemorySlot>(MemorySlot::kMemoryView0 + m_windowIndex);

    UpdateFont();
    SetMode(Mode::kModeByte);
    setFocus();
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(true);

    connect(m_pTargetModel, &TargetModel::memoryChangedSignal,      this, &MemoryWidget::memoryChanged);
    connect(m_pTargetModel, &TargetModel::startStopChangedSignal,   this, &MemoryWidget::startStopChanged);
    connect(m_pTargetModel, &TargetModel::registersChangedSignal,   this, &MemoryWidget::registersChanged);
    connect(m_pTargetModel, &TargetModel::connectChangedSignal,     this, &MemoryWidget::connectChanged);
    connect(m_pTargetModel, &TargetModel::otherMemoryChangedSignal, this, &MemoryWidget::otherMemoryChanged);
    connect(m_pTargetModel, &TargetModel::symbolTableChangedSignal, this, &MemoryWidget::symbolTableChanged);
    connect(m_pSession,     &Session::settingsChanged,              this, &MemoryWidget::settingsChanged);
}

MemoryWidget::~MemoryWidget()
{
}

bool MemoryWidget::GetAddressAtCursor(uint32_t& address) const
{
    // Search leftwards for the first valid character
    for (int col = m_cursorCol; col >= 0; --col)
    {
        const ColInfo& info = m_columnMap[col];
        if (info.type == ColInfo::kSpace)
            continue;
        address = m_rows[m_cursorRow].m_address + static_cast<uint32_t>(info.byteOffset);
        return true;
    }
    return false;
}

bool MemoryWidget::CanSetExpression(std::string expression) const
{
    uint32_t addr;
    return StringParsers::ParseExpression(expression.c_str(), addr,
                                        m_pTargetModel->GetSymbolTable(),
                                        m_pTargetModel->GetRegs());
}

bool MemoryWidget::SetExpression(std::string expression)
{
    // This does a "once only"
    uint32_t addr;
    if (!StringParsers::ParseExpression(expression.c_str(), addr,
                                        m_pTargetModel->GetSymbolTable(),
                                        m_pTargetModel->GetRegs()))
    {
        return false;
    }
    SetAddress(addr, kNoMoveCursor);
    m_addressExpression = expression;
    return true;
}

void MemoryWidget::SetSearchResultAddress(uint32_t addr)
{
    SetAddress(addr, kMoveCursor);
}

void MemoryWidget::SetAddress(uint32_t address, CursorMode moveCursor)
{
    m_address = address;
    RequestMemory(moveCursor);
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
}

void MemoryWidget::SetMode(MemoryWidget::Mode mode)
{
    m_mode = mode;

    // Calc the screen postions.
    // I need a screen position for each *character* on the grid (i.e. nybble)
    int groupSize = 0;
    if (m_mode == kModeByte)
        groupSize = 1;
    else if (m_mode == kModeWord)
        groupSize = 2;
    else if (m_mode == kModeLong)
        groupSize = 4;

    m_columnMap.clear();
    int byte = 0;
    while (byte + groupSize <= m_bytesPerRow)
    {
        ColInfo info;
        for (int subByte = 0; subByte < groupSize; ++subByte)
        {
            info.byteOffset = byte;
            info.type = ColInfo::kTopNybble;
            m_columnMap.push_back(info);
            info.type = ColInfo::kBottomNybble;
            m_columnMap.push_back(info);
            ++byte;
        }

        // Insert a space
        info.type = ColInfo::kSpace;
        m_columnMap.push_back(info);
    }

    // Add ASCII
    for (int byte2 = 0; byte2 < m_bytesPerRow; ++byte2)
    {
        ColInfo info;
        info.type = ColInfo::kASCII;
        info.byteOffset = byte2;
        m_columnMap.push_back(info);
    }

    // Stop crash when resizing and cursor is at end
    if (m_cursorCol >= m_columnMap.size())
        m_cursorCol = m_columnMap.size() - 1;

    RecalcText();
}

void MemoryWidget::MoveUp()
{
    if (m_cursorRow > 0)
    {
        --m_cursorRow;
        update();
        return;
    }
    MoveRelative(-m_bytesPerRow);
}

void MemoryWidget::MoveDown()
{
    if (m_cursorRow < m_rowCount - 1)
    {
        ++m_cursorRow;
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
    update();
}

void MemoryWidget::MoveRight()
{
    m_cursorCol++;
    if (m_cursorCol + 1 >= m_columnMap.size())
        m_cursorCol = m_columnMap.size() - 1;
    update();
}

void MemoryWidget::PageUp()
{
    if (m_cursorRow > 0)
    {
        m_cursorRow = 0;
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

    if (bytes >= 0)
    {
        uint32_t bytesAbs(static_cast<uint32_t>(bytes));
        SetAddress(m_address + bytesAbs, kNoMoveCursor);
    }
    else
    {
        // "bytes" is negative, so convert to unsigned for calcs
        uint32_t bytesAbs(static_cast<uint32_t>(-bytes));
        if (m_address > bytesAbs)
            SetAddress(m_address - bytesAbs, kNoMoveCursor);
        else
            SetAddress(0, kNoMoveCursor);
    }
}

bool MemoryWidget::EditKey(char key)
{
    // Can't edit while we still wait for memory
    if (m_requestId != 0)
        return false;

    const ColInfo& info = m_columnMap[m_cursorCol];
    if (info.type == ColInfo::kSpace)
    {
        MoveRight();
        return false;
    }

    uint8_t cursorByte = m_rows[m_cursorRow].m_rawBytes[info.byteOffset];
    uint32_t address = m_rows[m_cursorRow].m_address + static_cast<uint32_t>(info.byteOffset);
    uint8_t val;
    if (info.type == ColInfo::kBottomNybble)
    {
        if (!StringParsers::ParseHexChar(key, val))
            return false;

        cursorByte &= 0xf0;
        cursorByte |= val;
    }
    else if (info.type == ColInfo::kTopNybble)
    {
        if (!StringParsers::ParseHexChar(key, val))
            return false;
        cursorByte &= 0x0f;
        cursorByte |= val << 4;
    }
    else if (info.type == ColInfo::kASCII)
    {
        cursorByte = static_cast<uint8_t>(key);
    }

    QVector<uint8_t> data(1);
    data[0] = cursorByte;
    QString cmd = QString::asprintf("memset $%x 1 %02x", address, cursorByte);
    m_pDispatcher->WriteMemory(address, data);

    // Replace the value so that editing still works
    m_rows[m_cursorRow].m_rawBytes[info.byteOffset] = cursorByte;
    RecalcText();

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
    if (info.type == ColInfo::kSpace)
        return 0;

    uint8_t val;
    if (info.type == ColInfo::kBottomNybble)
    {
        if (!StringParsers::ParseHexChar(ascii, val))
            return 0;
        return ascii;
    }
    else if (info.type == ColInfo::kTopNybble)
    {
        if (!StringParsers::ParseHexChar(ascii, val))
            return 0;
        return ascii;
    }
    else if (info.type == ColInfo::kASCII)
    {
        if (ascii >= 32)
            return ascii;
    }
    return 0;
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

    if (pMem->GetAddress() != m_address)
        return;

    if (m_requestCursorMode == kMoveCursor)
    {
        m_cursorRow = 0;
        m_cursorCol = 0;
    }

    // We should just save the memory block here and format on demand
    // Build up memory in the rows
    int32_t rowCount = m_rowCount;
    uint32_t offset = 0;
    for (int32_t r = 0; r < rowCount; ++r)
    {
        Row row;
        row.m_address = pMem->GetAddress() + offset;
        row.m_rawBytes.clear();
        row.m_rawBytes.resize(m_bytesPerRow);
        for (int32_t i = 0; i < m_bytesPerRow; ++i)
        {
            if (offset == pMem->GetSize())
            {
                if (i != 0)
                    m_rows.push_back(row);      // add unfinished row
                break;
            }

            uint8_t c = pMem->Get(offset);
            row.m_rawBytes[i] = c;
            ++offset;
        }
        m_rows.push_back(row);
    }
    RecalcText();
    m_requestId = 0;
    m_requestCursorMode = kNoMoveCursor;
}

void MemoryWidget::RecalcText()
{
    const SymbolTable& symTable = m_pTargetModel->GetSymbolTable();

    static char toHex[] = "0123456789abcdef";
    int32_t rowCount = m_rows.size();
    int32_t colCount = m_columnMap.size();
    for (int32_t r = 0; r < rowCount; ++r)
    {
        Row& row = m_rows[r];
        row.m_text.resize(colCount);
        row.m_byteChanged.resize(colCount);
        row.m_symbolId.resize(colCount);

        for (int col = 0; col < colCount; ++col)
        {
            const ColInfo& info = m_columnMap[col];
            uint8_t byteVal = 0;
            char outChar = ' ';
            switch (info.type)
            {
            case ColInfo::kTopNybble:
                byteVal = row.m_rawBytes[info.byteOffset];
                outChar = toHex[(byteVal >> 4) & 0xf];
                break;
            case ColInfo::kBottomNybble:
                byteVal = row.m_rawBytes[info.byteOffset];
                outChar = toHex[byteVal & 0xf];
                break;
            case ColInfo::kASCII:
                byteVal = row.m_rawBytes[info.byteOffset];
                outChar = '.';
                if (byteVal >= 32 && byteVal < 128)
                    outChar = static_cast<char>(byteVal);
                break;
            case ColInfo::kSpace:
                break;
            }

            uint32_t addr = row.m_address + static_cast<uint32_t>(info.byteOffset);
            bool changed = false;
            if (m_previousMemory.HasAddress(addr))
            {
                uint8_t oldC = m_previousMemory.ReadAddressByte(addr);
                if (oldC != byteVal)
                    changed = true;
            }

            Symbol sym;
            row.m_symbolId[col] = -1;

            if (info.type != ColInfo::kSpace)
            {
                if (symTable.FindLowerOrEqual(addr, sym))
                {
                    row.m_symbolId[col] = (int)sym.index;
                }
            }
            row.m_text[col] = outChar;
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
            m_previousMemory = Memory(0, 0);
        }
    }
}

void MemoryWidget::connectChanged()
{
    m_rows.clear();
    m_address = 0;
    m_rowCount = 10;
    update();
}

void MemoryWidget::registersChanged()
{
    // New registers can affect expression parsing
    RecalcLockedExpression();

    // Always re-request here, since this is expected after every start/stop
    RequestMemory(kNoMoveCursor);
}

void MemoryWidget::otherMemoryChanged(uint32_t address, uint32_t size)
{
    (void)address;
    (void)size;
    // Do a re-request
    // TODO only re-request if it affected our view...
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

void MemoryWidget::paintEvent(QPaintEvent* ev)
{
    QWidget::paintEvent(ev);

    // CAREFUL! This could lead to an infinite loop of redraws if we are not.
    RecalcRowCount();

    QPainter painter(this);
    const QPalette& pal = this->palette();

    if (m_rows.size() != 0)
    {
        painter.setFont(m_monoFont);
        QFontMetrics info(painter.fontMetrics());

        // Compensate for text descenders
        int y_ascent = info.ascent();
        int char_width = info.horizontalAdvance("0");

        // Set up the rendering info
        m_charWidth = char_width;

        QColor backCol = pal.background().color();
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
            const Row& r = m_rows[row];

            // Draw address string
            painter.setPen(pal.text().color());
            int topleft_y = GetPixelFromRow(row);
            int text_y = topleft_y + y_ascent;
            QString addr = QString::asprintf("%08x", r.m_address);
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
        if (m_cursorRow >= 0 && m_cursorRow < m_rows.size())
        {
            int y_curs = GetPixelFromRow(m_cursorRow);
            int x_curs = GetPixelFromCol(m_cursorCol);

            painter.setBrush(pal.highlight());
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
        switch (event->key())
        {
        case Qt::Key_Left:       MoveLeft();          return;
        case Qt::Key_Right:      MoveRight();         return;
        case Qt::Key_Up:         MoveUp();            return;
        case Qt::Key_Down:       MoveDown();          return;
        case Qt::Key_PageUp:     PageUp();            return;
        case Qt::Key_PageDown:   PageDown();          return;
        default: break;
        }

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
    if (event->button() == Qt::MouseButton::LeftButton)
    {
        // Over a hex char
        int x = static_cast<int>(event->localPos().x());
        int y = static_cast<int>(event->localPos().y());
        int row;
        int col;
        if (FindInfo(x, y, row, col))
        {
            m_cursorCol = col;
            m_cursorRow = row;
            update();
        }
    }
    QWidget::mousePressEvent(event);
}

void MemoryWidget::wheelEvent(QWheelEvent *event)
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
    if (!FindInfo(x, y, row, col))
        return;

    if (m_columnMap[col].type == ColInfo::kSpace)
        return;

    // Align the memory location to 2 or 4 bytes, based on context
    // (view address, or word/long mode)
    uint32_t byteOffset = m_columnMap[col].byteOffset;
    byteOffset &= ~1U;
    if (m_mode == Mode::kModeLong)
        byteOffset &= ~3U;

    uint32_t addr = m_rows[row].m_address + byteOffset;

    QMenu menu(this);
    m_showAddressMenus[0].setAddress(m_pSession, addr);
    m_showAddressMenus[0].setTitle(QString::asprintf("Data Address: $%x", addr));
    menu.addMenu(m_showAddressMenus[0].m_pMenu);

    const Memory* mem = m_pTargetModel->GetMemory(m_memSlot);
    if (mem)
    {
        if (mem->HasAddressMulti(addr, 4))
        {
            // Read a longword
            uint32_t longContents = mem->ReadAddressMulti(addr, 4);
            longContents &= 0xffffff;

            m_showAddressMenus[1].setAddress(m_pSession, longContents);
            m_showAddressMenus[1].setTitle(QString::asprintf("Pointer Address: $%x", longContents));
            menu.addMenu(m_showAddressMenus[1].m_pMenu);
        }
    }

    // Run it
    menu.exec(event->globalPos());
}

void MemoryWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    RecalcRowCount();
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
    uint32_t size = static_cast<uint32_t>(m_rowCount * m_bytesPerRow);
    if (m_pTargetModel->IsConnected())
    {
        m_requestId = m_pDispatcher->ReadMemory(m_memSlot, m_address, size);
        m_requestCursorMode = moveCursor;
    }
}

void MemoryWidget::RecalcLockedExpression()
{
    if (m_isLocked)
    {
        uint32_t addr;
        if (StringParsers::ParseExpression(m_addressExpression.c_str(), addr,
                                            m_pTargetModel->GetSymbolTable(),
                                            m_pTargetModel->GetRegs()))
        {
            SetAddress(addr, kNoMoveCursor);
        }
    }

}

void MemoryWidget::RecalcRowCount()
{
    // It seems that viewport is updated without this even being called,
    // which means that on startup, "h" == 0.
    int h = this->size().height() - (Session::kWidgetBorderY * 2);
    int rowh = m_lineHeight;
    int count = 0;
    if (rowh != 0)
        count = h / rowh;
    SetRowCount(count);

    if (m_cursorRow >= m_rowCount)
    {
        m_cursorRow = m_rowCount - 1;
    }
}

QString MemoryWidget::CalcMouseoverText(int mouseX, int mouseY)
{
    int row;
    int col;
    if (!FindInfo(mouseX, mouseY, row, col))
        return QString();

    if (m_columnMap[col].type == ColInfo::kSpace)
            return QString();

    const Memory* mem = m_pTargetModel->GetMemory(m_memSlot);
    if (!mem)
        return QString();

    uint32_t addr = m_rows[row].m_address + m_columnMap[col].byteOffset;
    uint8_t byteVal = mem->ReadAddressByte(addr);

    if (!mem->HasAddressMulti(addr & 0xfffffe, 2))
        return QString();

    uint32_t wordVal = mem->ReadAddressMulti(addr & 0xfffffe, 2);

    // Work out what's under the mouse

    // Align the memory location to 2 or 4 bytes, based on context
    // (view address, or word/long mode)
    uint32_t byteOffset = m_columnMap[col].byteOffset;
    byteOffset &= ~1U;
    if (m_mode == Mode::kModeLong)
        byteOffset &= ~3U;

    uint32_t addrLong = m_rows[row].m_address + byteOffset;
    if (!mem->HasAddressMulti(addrLong, 4))
        return QString();

    uint32_t longVal = mem->ReadAddressMulti(addrLong, 4);
    return CreateTooltip(addr, m_pTargetModel->GetSymbolTable(), byteVal, wordVal, longVal);
}

void MemoryWidget::UpdateFont()
{
    m_monoFont = m_pSession->GetSettings().m_font;
    QFontMetrics info(m_monoFont);
    m_lineHeight = info.lineSpacing();
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

int MemoryWidget::GetPixelFromCol(int column) const
{
    return GetAddrX() + (10 + column) * m_charWidth;
}

bool MemoryWidget::FindInfo(int x, int y, int& row, int& col)
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

    m_pSymbolTableModel = new SymbolTableModel(this, m_pTargetModel->GetSymbolTable());
    QCompleter* pCompl = new QCompleter(m_pSymbolTableModel, this);
    pCompl->setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);

    // Construction. Do in order of tabbing
    m_pMemoryWidget = new MemoryWidget(this, pSession, windowIndex);
    m_pMemoryWidget->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));

    m_pAddressEdit = new QLineEdit(this);
    m_pAddressEdit->setCompleter(pCompl);

    m_pLockCheckBox = new QCheckBox(tr("Lock"), this);

    m_pComboBox = new QComboBox(this);
    m_pComboBox->insertItem(MemoryWidget::kModeByte, "Byte");
    m_pComboBox->insertItem(MemoryWidget::kModeWord, "Word");
    m_pComboBox->insertItem(MemoryWidget::kModeLong, "Long");
    m_pComboBox->setCurrentIndex(m_pMemoryWidget->GetMode());

    // Layouts
    QVBoxLayout* pMainLayout = new QVBoxLayout;
    QHBoxLayout* pTopLayout = new QHBoxLayout;
    auto pMainRegion = new QWidget(this);   // whole panel
    auto pTopRegion = new QWidget(this);      // top buttons/edits

    SetMargins(pTopLayout);
    pTopLayout->addWidget(m_pAddressEdit);
    pTopLayout->addWidget(m_pLockCheckBox);
    pTopLayout->addWidget(m_pComboBox);

    SetMargins(pMainLayout);
    pMainLayout->addWidget(pTopRegion);
    pMainLayout->addWidget(m_pMemoryWidget);

    pTopRegion->setLayout(pTopLayout);
    pMainRegion->setLayout(pMainLayout);
    setWidget(pMainRegion);

    loadSettings();

    // The scope here is explained at https://forum.qt.io/topic/67981/qshortcut-multiple-widget-instances/2
    new QShortcut(QKeySequence("Ctrl+F"),         this, SLOT(findClickedSlot()), nullptr, Qt::WidgetWithChildrenShortcut);
    new QShortcut(QKeySequence("F3"),             this, SLOT(nextClickedSlot()), nullptr, Qt::WidgetWithChildrenShortcut);
    new QShortcut(QKeySequence("Ctrl+G"),         this, SLOT(gotoClickedSlot()), nullptr, Qt::WidgetWithChildrenShortcut);
    new QShortcut(QKeySequence("Ctrl+L"),         this, SLOT(lockClickedSlot()), nullptr, Qt::WidgetWithChildrenShortcut);

    connect(m_pAddressEdit,  &QLineEdit::returnPressed,        this, &MemoryWindow::returnPressedSlot);
    connect(m_pAddressEdit,  &QLineEdit::textChanged,          this, &MemoryWindow::textEditedSlot);
    connect(m_pLockCheckBox, &QCheckBox::stateChanged,         this, &MemoryWindow::lockChangedSlot);
    connect(m_pSession,      &Session::addressRequested,       this, &MemoryWindow::requestAddress);
    connect(m_pTargetModel,  &TargetModel::searchResultsChangedSignal, this, &MemoryWindow::searchResultsSlot);
    connect(m_pComboBox,     SIGNAL(currentIndexChanged(int)), SLOT(modeComboBoxChanged(int)));
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
    int mode = settings.value("mode", QVariant(0)).toInt();
    m_pMemoryWidget->SetMode(static_cast<MemoryWidget::Mode>(mode));
    m_pComboBox->setCurrentIndex(m_pMemoryWidget->GetMode());
    settings.endGroup();
}

void MemoryWindow::saveSettings()
{
    QSettings settings;
    QString key = QString::asprintf("MemoryView%d", m_windowIndex);
    settings.beginGroup(key);

    settings.setValue("geometry", saveGeometry());
    settings.setValue("mode", static_cast<int>(m_pMemoryWidget->GetMode()));
    settings.endGroup();
}

void MemoryWindow::requestAddress(Session::WindowType type, int windowIndex, uint32_t address)
{
    if (type != Session::WindowType::kMemoryWindow)
        return;

    if (windowIndex != m_windowIndex)
        return;

    m_pMemoryWidget->SetLock(false);
    m_pMemoryWidget->SetExpression(std::to_string(address));
    m_pLockCheckBox->setChecked(false);
    setVisible(true);
    this->keyFocus();
}

void MemoryWindow::returnPressedSlot()
{
    bool valid = m_pMemoryWidget->SetExpression(m_pAddressEdit->text().toStdString());
    Colouring::SetErrorState(m_pAddressEdit, valid);
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

void MemoryWindow::modeComboBoxChanged(int index)
{
    m_pMemoryWidget->SetMode((MemoryWidget::Mode)index);
}

void MemoryWindow::findClickedSlot()
{
    if (!m_pTargetModel->IsConnected())
        return;

    uint32_t addr;
    if (!m_pMemoryWidget->GetAddressAtCursor(addr))
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

void MemoryWindow::nextClickedSlot()
{
    if (!m_pTargetModel->IsConnected())
        return;

    uint32_t addr;
    if (!m_pMemoryWidget->GetAddressAtCursor(addr))
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

void MemoryWindow::gotoClickedSlot()
{
    m_pAddressEdit->setFocus();
}

void MemoryWindow::lockClickedSlot()
{
    m_pLockCheckBox->toggle();
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
            m_pLockCheckBox->setChecked(false);
            m_pMemoryWidget->SetSearchResultAddress(addr);

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
