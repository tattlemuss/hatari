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

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../models/stringparsers.h"
#include "../models/symboltablemodel.h"
#include "../models/session.h"
#include "quicklayout.h"

MemoryWidget::MemoryWidget(QWidget *parent, Session* pSession,
                                           int windowIndex) :
    QWidget(parent),
    m_pSession(pSession),
    m_pTargetModel(pSession->m_pTargetModel),
    m_pDispatcher(pSession->m_pDispatcher),
    m_isLocked(false),
    m_address(0),
    m_bytesPerRow(16),
    m_mode(kModeByte),
    m_rowCount(1),
    m_requestId(0),
    m_windowIndex(windowIndex),
    m_previousMemory(0, 0),
    m_cursorRow(0),
    m_cursorCol(0)
{
    m_memSlot = static_cast<MemorySlot>(MemorySlot::kMemoryView0 + m_windowIndex);

    UpdateFont();
    SetMode(Mode::kModeByte);
    setFocus();
    setFocusPolicy(Qt::StrongFocus);
    connect(m_pTargetModel, &TargetModel::memoryChangedSignal,      this, &MemoryWidget::memoryChangedSlot);
    connect(m_pTargetModel, &TargetModel::startStopChangedSignal,   this, &MemoryWidget::startStopChangedSlot);
    connect(m_pTargetModel, &TargetModel::connectChangedSignal,     this, &MemoryWidget::connectChangedSlot);
    connect(m_pTargetModel, &TargetModel::otherMemoryChanged,       this, &MemoryWidget::otherMemoryChangedSlot);
    connect(m_pSession,     &Session::settingsChanged,              this, &MemoryWidget::settingsChangedSlot);
}

bool MemoryWidget::SetAddress(std::string expression)
{
    uint32_t addr;
    if (!StringParsers::ParseExpression(expression.c_str(), addr,
                                        m_pTargetModel->GetSymbolTable(),
                                        m_pTargetModel->GetRegs()))
    {
        return false;
    }
    SetAddress(addr);
    m_addressExpression = expression;
    return true;
}

void MemoryWidget::SetAddress(uint32_t address)
{
    m_address = address;
    RequestMemory();
}

void MemoryWidget::SetRowCount(int32_t rowCount)
{
    // Handle awkward cases by always having at least one row
    if (rowCount < 1)
        rowCount = 1;
    if (rowCount != m_rowCount)
    {
        m_rowCount = rowCount;
        RequestMemory();

        if (m_cursorRow >= m_rowCount)
        {
            m_cursorRow = m_rowCount - 1;
        }
    }
}

void MemoryWidget::SetLock(bool locked)
{
    if (!locked != m_isLocked)
    {
        if (locked)
        {
            // Recalculate this expression for locking
            SetAddress(m_addressExpression);
        }
    }
    m_isLocked = locked;
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

    m_columnPositions.clear();
    for (int i = 0; i < m_bytesPerRow; ++i)
    {
        int group = i / groupSize;     // group of N bytes (N * chars)
        int byteInGroup = i % groupSize;

        // Calc the left-hand X position of this byte
        int base_x = group * (groupSize * 2 + 1) + 2 * byteInGroup;

        // top nybble
        m_columnPositions.push_back(base_x);
        // bottom nybble
        m_columnPositions.push_back(base_x + 1);
    }

    RecalcText();
}

void MemoryWidget::MoveUp()
{
    if (m_cursorRow > 0)
    {
        --m_cursorRow;
        repaint();
        return;
    }

    if (m_requestId != 0)
        return; // not up to date

    if (m_address >= m_bytesPerRow)
        SetAddress(m_address - m_bytesPerRow);
    else
        SetAddress(0);
}

void MemoryWidget::MoveDown()
{
    if (m_cursorRow < m_rowCount - 1)
    {
        ++m_cursorRow;
        repaint();
        return;
    }

    if (m_requestId != 0)
        return; // not up to date

    SetAddress(m_address + m_bytesPerRow);
}

void MemoryWidget::MoveLeft()
{
    if (m_cursorCol == 0)
        return;

    m_cursorCol--;
    repaint();
}

void MemoryWidget::MoveRight()
{
    if (m_cursorCol + 2 >= m_bytesPerRow * 2 + 1)
        return;

    m_cursorCol++;
    repaint();
}

void MemoryWidget::PageUp()
{
    if (m_cursorRow > 0)
    {
        m_cursorRow = 0;
        repaint();
        return;
    }

    if (m_requestId != 0)
        return; // not up to date

    if (m_address > m_bytesPerRow * m_rowCount)
        SetAddress(m_address - m_bytesPerRow * m_rowCount);
    else
        SetAddress(0);
}

void MemoryWidget::PageDown()
{
    if (m_cursorRow < m_rowCount - 1)
    {
        m_cursorRow = m_rowCount - 1;
        repaint();
        return;
    }

    if (m_requestId != 0)
        return; // not up to date

    SetAddress(m_address + m_bytesPerRow * m_rowCount);
}

void MemoryWidget::EditKey(uint8_t val)
{
    // Can't edit while we still wait for memory
    if (m_requestId != 0)
        return;
    uint32_t address;
    bool lowNybble;
    GetCursorInfo(address, lowNybble);

    uint8_t nybbles[2];
    uint8_t cursorByte = m_rows[m_cursorRow].m_rawBytes[m_cursorCol / 2];
    if (lowNybble)
    {
        // This is the low nybble
        nybbles[0] = cursorByte & 0xf0;
        nybbles[1] = val;
    }
    else
    {
        nybbles[0] = val << 4;
        nybbles[1] = cursorByte & 0xf;
    }
    uint8_t finalVal = nybbles[0] | nybbles[1];
    QString cmd = QString::asprintf("memset $%x 1 %02x", address, finalVal);

    std::cout << cmd.toStdString() << std::endl;

    m_pDispatcher->SendCommandPacket(cmd.toStdString().c_str());

    // Replace the value so that editing still works
    m_rows[m_cursorRow].m_rawBytes[m_cursorCol / 2] = finalVal;
    RecalcText();

    MoveRight();
}

void MemoryWidget::memoryChangedSlot(int memorySlot, uint64_t commandId)
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
}

void MemoryWidget::RecalcText()
{
    int32_t rowCount = m_rows.size();
    for (int32_t r = 0; r < rowCount; ++r)
    {
        Row& row = m_rows[r];
        row.m_hexText.clear();
        row.m_asciiText.clear();
        row.m_byteChanged.resize(row.m_rawBytes.size());

        for (int32_t i = 0; i < row.m_rawBytes.size(); ++i)
        {
            uint8_t c = row.m_rawBytes[i];
            row.m_hexText += QString::asprintf("%02x", c);

            if (c >= 32 && c < 128)
                row.m_asciiText += QString::asprintf("%c", c);
            else
                row.m_asciiText += ".";

            uint32_t addr = row.m_address + static_cast<uint32_t>(i);
            bool changed = false;
            if (m_previousMemory.HasAddress(addr))
            {
                uint8_t oldC = m_previousMemory.ReadAddressByte(addr);
                if (oldC != c)
                    changed = true;
            }
            row.m_byteChanged[i] = changed;
        }
    }
    repaint();
}

void MemoryWidget::startStopChangedSlot()
{
    // Request new memory for the view
    if (!m_pTargetModel->IsRunning())
    {
        // Recalc a locked expression
        uint32_t addr;
        if (m_isLocked)
        {
            if (StringParsers::ParseExpression(m_addressExpression.c_str(), addr,
                                                m_pTargetModel->GetSymbolTable(),
                                                m_pTargetModel->GetRegs()))
            {
                SetAddress(addr);
            }
        }
        RequestMemory();
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

void MemoryWidget::connectChangedSlot()
{
    m_rows.clear();
    m_address = 0;
    m_rowCount = 10;
    repaint();
}

void MemoryWidget::otherMemoryChangedSlot(uint32_t address, uint32_t size)
{
    (void)address;
    (void)size;
    // Do a re-request
    // TODO only re-request if it affected our view...
    RequestMemory();
}

void MemoryWidget::settingsChangedSlot()
{
    UpdateFont();
    RecalcRowCount();
    RequestMemory();
    update();
}

void MemoryWidget::paintEvent(QPaintEvent* ev)
{
    QWidget::paintEvent(ev);

    // CAREFUL! This could lead to an infinite loop of redraws if we are not.
    RecalcRowCount();

    QPainter painter(this);
    painter.setFont(m_monoFont);
    QFontMetrics info(painter.fontMetrics());
    const QPalette& pal = this->palette();

    const QBrush& br = pal.background().color();
    painter.fillRect(this->rect(), br);
    painter.setPen(QPen(pal.dark(), hasFocus() ? 6 : 2));
    painter.drawRect(this->rect());

    if (m_rows.size() == 0)
        return;

    // Compensate for text descenders
    int y_ascent = info.ascent();
    int char_width = info.horizontalAdvance("0");

    // Set up the rendering info
    m_charWidth = char_width;

    painter.setPen(pal.text().color());
    for (int row = 0; row < m_rows.size(); ++row)
    {
        const Row& r = m_rows[row];

        // Draw address string
        painter.setPen(pal.text().color());
        int text_y = GetPixelFromRow(row) + y_ascent;
        QString addr = QString::asprintf("%08x", r.m_address);
        painter.drawText(GetAddrX(), text_y, addr);

        // Now hex
        // We write out the values per-nybble
        for (int col = 0; col < m_columnPositions.size(); ++col)
        {
            int byteOffset = col / 2;
            bool changed = r.m_byteChanged[byteOffset];
            painter.setPen(changed ? Qt::red : pal.text().color());

            int x = GetHexCharX(col);
            QChar st = r.m_hexText.at(col);
            painter.drawText(x, text_y, st);
        }

        for (int col = 0; col < r.m_asciiText.size(); ++col)
        {
            int x_ascii = GetAsciiCharX(col);
            bool changed = r.m_byteChanged[col];
            painter.setPen(changed ? Qt::red : pal.text().color());
            QString t = r.m_asciiText.at(col);
            painter.drawText(x_ascii, text_y, t);
        }
    }

    // Draw highlight/cursor area in the hex
    if (m_cursorRow >= 0 && m_cursorRow < m_rows.size())
    {
        int y_curs = GetPixelFromRow(m_cursorRow);
        int x_curs = GetHexCharX(m_cursorCol);

        painter.setBrush(pal.highlight());
        painter.drawRect(x_curs, y_curs, char_width, m_lineHeight);

        QChar st = m_rows[m_cursorRow].m_hexText.at(m_cursorCol);
        painter.setPen(pal.highlightedText().color());
        painter.drawText(x_curs, y_ascent + y_curs, st);
    }
}

void MemoryWidget::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
    case Qt::Key_Left:       MoveLeft();          return;
    case Qt::Key_Right:      MoveRight();         return;
    case Qt::Key_Up:         MoveUp();            return;
    case Qt::Key_Down:       MoveDown();          return;
    case Qt::Key_PageUp:     PageUp();            return;
    case Qt::Key_PageDown:   PageDown();          return;

    case Qt::Key_0:          EditKey(0); return;
    case Qt::Key_1:          EditKey(1); return;
    case Qt::Key_2:          EditKey(2); return;
    case Qt::Key_3:          EditKey(3); return;
    case Qt::Key_4:          EditKey(4); return;
    case Qt::Key_5:          EditKey(5); return;
    case Qt::Key_6:          EditKey(6); return;
    case Qt::Key_7:          EditKey(7); return;
    case Qt::Key_8:          EditKey(8); return;
    case Qt::Key_9:          EditKey(9); return;
    case Qt::Key_A:          EditKey(10); return;
    case Qt::Key_B:          EditKey(11); return;
    case Qt::Key_C:          EditKey(12); return;
    case Qt::Key_D:          EditKey(13); return;
    case Qt::Key_E:          EditKey(14); return;
    case Qt::Key_F:          EditKey(15); return;
    default: break;
    }
    QWidget::keyPressEvent(event);
}

void MemoryWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MouseButton::LeftButton)
    {
        // Over a hex char
        int x = (int)event->localPos().x();
        int y = (int)event->localPos().y();

        int row = GetRowFromPixel(y);
        if (row > 0 && row < m_rows.size())
        {
            // Find the X char that might fit
            for (int col = 0; col < m_columnPositions.size(); ++col)
            {
                int charPos = GetHexCharX(col);
                if (x >= charPos && x < charPos + m_charWidth)
                {
                    m_cursorCol = col;
                    m_cursorRow = row;
                    repaint();
                    break;
                }
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void MemoryWidget::RequestMemory()
{
    uint32_t size = ((m_rowCount * 16));
    if (m_pTargetModel->IsConnected())
        m_requestId = m_pDispatcher->RequestMemory(m_memSlot, m_address, size);
}

void MemoryWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    RecalcRowCount();
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

int MemoryWidget::GetHexCharX(int column) const
{
    assert(column < m_columnPositions.size());
    return GetAddrX() + (10 + m_columnPositions[column]) * m_charWidth;
}

int MemoryWidget::GetAsciiCharX(int column) const
{
    int32_t lastCharX = m_columnPositions.back();
    return GetAddrX() + (10 + lastCharX + 3 + column) * m_charWidth;
}

void MemoryWidget::GetCursorInfo(uint32_t &address, bool &bottomNybble)
{
    address = m_rows[m_cursorRow].m_address;
    address += (m_cursorCol / 2);
    bottomNybble = (m_cursorCol & 1) ? true : false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
MemoryWindow::MemoryWindow(QWidget *parent, Session* pSession, int windowIndex) :
    QDockWidget(parent),
    m_pSession(pSession),
    m_pTargetModel(pSession->m_pTargetModel),
    m_pDispatcher(pSession->m_pDispatcher),
    m_windowIndex(windowIndex)
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

    m_pLineEdit = new QLineEdit(this);
    m_pLineEdit->setCompleter(pCompl);

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
    pTopLayout->addWidget(m_pLineEdit);
    pTopLayout->addWidget(m_pLockCheckBox);
    pTopLayout->addWidget(m_pComboBox);

    SetMargins(pMainLayout);
    pMainLayout->addWidget(pTopRegion);
    pMainLayout->addWidget(m_pMemoryWidget);

    pTopRegion->setLayout(pTopLayout);
    pMainRegion->setLayout(pMainLayout);
    setWidget(pMainRegion);

    loadSettings();

    // Listen for start/stop, so we can update our memory request
    connect(m_pLineEdit, &QLineEdit::returnPressed,         this, &MemoryWindow::textEditChangedSlot);
    connect(m_pLockCheckBox, &QCheckBox::stateChanged,      this, &MemoryWindow::lockChangedSlot);
    connect(m_pComboBox, SIGNAL(currentIndexChanged(int)),  SLOT(modeComboBoxChanged(int)));
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

void MemoryWindow::requestAddress(int windowIndex, bool isMemory, uint32_t address)
{
    if (!isMemory)
        return;

    if (windowIndex != m_windowIndex)
        return;

    m_pMemoryWidget->SetLock(false);
    m_pMemoryWidget->SetAddress(std::to_string(address));
    m_pLockCheckBox->setChecked(false);
    setVisible(true);
    this->keyFocus();
}


void MemoryWindow::textEditChangedSlot()
{
    m_pMemoryWidget->SetAddress(m_pLineEdit->text().toStdString());
}

void MemoryWindow::lockChangedSlot()
{
    m_pMemoryWidget->SetLock(m_pLockCheckBox->isChecked());
}

void MemoryWindow::modeComboBoxChanged(int index)
{
    m_pMemoryWidget->SetMode((MemoryWidget::Mode)index);
}
