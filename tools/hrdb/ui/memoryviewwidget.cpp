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

MemoryWidget::MemoryWidget(QWidget *parent, TargetModel *pTargetModel, Dispatcher* pDispatcher,
                                           int windowIndex) :
    QWidget(parent),
    m_pTargetModel(pTargetModel),
    m_pDispatcher(pDispatcher),
    m_isLocked(false),
    m_address(0),
    m_bytesPerRow(16),
    m_mode(kModeByte),
    m_rowCount(1),
    m_requestId(0),
    m_windowIndex(windowIndex),
    m_cursorRow(0),
    m_cursorCol(0)
{
    m_memSlot = (MemorySlot)(MemorySlot::kMemoryView0 + m_windowIndex);

    RecalcSizes();
    SetMode(Mode::kModeByte);
    setFocus();
    setFocusPolicy(Qt::StrongFocus);
    connect(m_pTargetModel, &TargetModel::memoryChangedSignal,      this, &MemoryWidget::memoryChangedSlot);
    connect(m_pTargetModel, &TargetModel::startStopChangedSignal,   this, &MemoryWidget::startStopChangedSlot);
    connect(m_pTargetModel, &TargetModel::connectChangedSignal,     this, &MemoryWidget::connectChangedSlot);
    connect(m_pTargetModel, &TargetModel::otherMemoryChanged,       this, &MemoryWidget::otherMemoryChangedSlot);
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

void MemoryWidget::SetRowCount(uint32_t rowCount)
{
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

    uint32_t groupSize = 0;
    if (m_mode == kModeByte)
        groupSize = 1;
    else if (m_mode == kModeWord)
        groupSize = 2;
    else if (m_mode == kModeLong)
        groupSize = 4;

    m_columnPositions.clear();
    for (uint32_t i = 0; i < m_bytesPerRow; ++i)
    {
        uint32_t group = i / groupSize;     // group of N bytes (N * chars)
        uint32_t byteInGroup = i % groupSize;

        // Calc the left-hand X position of this byte
        uint32_t base_x = group * (groupSize * 2 + 1) + 2 * byteInGroup;

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
    uint32_t rowCount = m_rowCount;
    uint32_t offset = 0;
    for (uint32_t r = 0; r < rowCount; ++r)
    {
        Row row;
        row.m_address = pMem->GetAddress() + offset;
        row.m_rawBytes.clear();
        row.m_rawBytes.resize(m_bytesPerRow);
        for (uint32_t i = 0; i < m_bytesPerRow; ++i)
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
    uint32_t rowCount = m_rows.size();
    for (uint32_t r = 0; r < rowCount; ++r)
    {
        Row& row = m_rows[r];
        row.m_hexText.clear();
        row.m_asciiText.clear();
        for (uint32_t i = 0; i < row.m_rawBytes.size(); ++i)
        {
            uint8_t c = row.m_rawBytes[i];
            row.m_hexText += QString::asprintf("%02x", c);

            if (c >= 32 && c < 128)
                row.m_asciiText += QString::asprintf("%c", c);
            else
                row.m_asciiText += ".";
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
    // Do a re-request
    // TODO only re-request if it affected our view...
    RequestMemory();
}

void MemoryWidget::paintEvent(QPaintEvent* ev)
{
    QWidget::paintEvent(ev);

#if 1
    // CAREFUL! This could lead to an infinite loop of redraws if we are not.
    RecalcRowCount();

    if (m_rows.size() == 0)
        return;

    QPainter painter(this);
    painter.setFont(monoFont);
    QFontMetrics info(painter.fontMetrics());
    const QPalette& pal = this->palette();

    const QBrush& br = pal.background().color();
    painter.fillRect(this->rect(), br);
    if (hasFocus())
    {
        painter.setPen(QPen(pal.dark(), 6));
        painter.drawRect(this->rect());
    }

    int y_base = info.ascent();
    int char_width = info.horizontalAdvance("0");

    // Set up the rendering info
    m_charWidth = char_width;

    painter.setPen(pal.text().color());
    for (size_t row = 0; row < m_rows.size(); ++row)
    {
        const Row& r = m_rows[row];

        // Draw address string
        painter.setPen(pal.text().color());
        int y = y_base + row * m_lineHeight;       // compensate for descenders TODO use ascent()
        QString addr = QString::asprintf("%08x", r.m_address);
        painter.drawText(GetAddrX(), y, addr);

        // Now hex
        // We write out the values per-nybble
        for (size_t col = 0; col < m_columnPositions.size(); ++col)
        {
            //size_t byteOffset = i / 2;
            //uint32_t shiftDown = (i % 2) * 4;

            int x = GetHexCharX(col);
            QChar st = r.m_hexText.at(col);
            painter.drawText(x, y, st);
        }

        int x_ascii = GetAsciiCharX();
        painter.drawText(x_ascii, y, m_rows[row].m_asciiText);
    }

    // Draw highlight/cursor area in the hex
    if (m_cursorRow >= 0 && m_cursorRow < m_rows.size())
    {
        int y_curs = m_cursorRow * m_lineHeight;       // compensate for descenders TODO use ascent()
        int x_curs = GetHexCharX(m_cursorCol);

        painter.setBrush(pal.highlight());
        painter.drawRect(x_curs, y_curs, char_width, m_lineHeight);

        QChar st = m_rows[m_cursorRow].m_hexText.at(m_cursorCol);
        painter.setPen(pal.highlightedText().color());
        painter.drawText(x_curs, y_base + y_curs, st);
    }
#endif
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

        int row = y / m_lineHeight;
        if (row < m_rows.size())
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
    int h = this->size().height();
    int rowh = m_lineHeight;
    if (rowh != 0)
        this->SetRowCount(h / rowh);

    if (m_cursorRow >= m_rowCount)
    {
        m_cursorRow = m_rowCount - 1;
    }
}

void MemoryWidget::RecalcSizes()
{
    monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QFontMetrics info(monoFont);
    m_lineHeight = info.lineSpacing();
}

// Position in pixels of address column
int MemoryWidget::GetAddrX() const
{
    return 10;
}

int MemoryWidget::GetHexCharX(int column) const
{
    assert(column < m_columnPositions.size());
    return GetAddrX() + (10 + m_columnPositions[column]) * m_charWidth;
}

int MemoryWidget::GetAsciiCharX() const
{
    uint32_t lastCharX = m_columnPositions.back();
    return GetAddrX() + (10 + lastCharX + 3) * m_charWidth;
}

void MemoryWidget::GetCursorInfo(uint32_t &address, bool &bottomNybble)
{
    address = m_rows[m_cursorRow].m_address;
    address += (m_cursorCol / 2);
    bottomNybble = (m_cursorCol & 1) ? true : false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
MemoryViewWidget::MemoryViewWidget(QWidget *parent, TargetModel* pTargetModel, Dispatcher* pDispatcher, int windowIndex) :
    QDockWidget(parent),
    m_pTargetModel(pTargetModel),
    m_pDispatcher(pDispatcher),
    m_windowIndex(windowIndex)
{
    this->setWindowTitle(QString::asprintf("Memory %d", windowIndex + 1));
    QString key = QString::asprintf("MemoryView%d", m_windowIndex);
    setObjectName(key);

    // Make the data first
    m_pMemoryWidget = new MemoryWidget(this, pTargetModel, pDispatcher, windowIndex);
    m_pMemoryWidget->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));

    m_pComboBox = new QComboBox(this);
    m_pComboBox->insertItem(MemoryWidget::kModeByte, "Byte");
    m_pComboBox->insertItem(MemoryWidget::kModeWord, "Word");
    m_pComboBox->insertItem(MemoryWidget::kModeLong, "Long");
    m_pComboBox->setCurrentIndex(m_pMemoryWidget->GetMode());

    m_pLockCheckBox = new QCheckBox(tr("Lock"), this);

    m_pLineEdit = new QLineEdit(this);
    m_pSymbolTableModel = new SymbolTableModel(this, m_pTargetModel->GetSymbolTable());
    QCompleter* pCompl = new QCompleter(m_pSymbolTableModel, this);
    pCompl->setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);

    m_pLineEdit->setCompleter(pCompl);

    // Layouts
    QVBoxLayout* pMainLayout = new QVBoxLayout;
    QHBoxLayout* pTopLayout = new QHBoxLayout;
    auto pMainRegion = new QWidget(this);   // whole panel
    auto pTopRegion = new QWidget(this);      // top buttons/edits

    pTopLayout->addWidget(m_pLineEdit);
    pTopLayout->addWidget(m_pLockCheckBox);
    pTopLayout->addWidget(m_pComboBox);

    pMainLayout->addWidget(pTopRegion);
    pMainLayout->addWidget(m_pMemoryWidget);

    pTopRegion->setLayout(pTopLayout);
    pMainRegion->setLayout(pMainLayout);
    setWidget(pMainRegion);

    loadSettings();

    // Listen for start/stop, so we can update our memory request
    connect(m_pLineEdit, &QLineEdit::returnPressed,         this, &MemoryViewWidget::textEditChangedSlot);
    connect(m_pLockCheckBox, &QCheckBox::stateChanged,      this, &MemoryViewWidget::lockChangedSlot);
    connect(m_pComboBox, SIGNAL(currentIndexChanged(int)),  SLOT(modeComboBoxChanged(int)));
}

void MemoryViewWidget::keyFocus()
{
    activateWindow();
    m_pMemoryWidget->setFocus();
}


void MemoryViewWidget::loadSettings()
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

void MemoryViewWidget::saveSettings()
{
    QSettings settings;
    QString key = QString::asprintf("MemoryView%d", m_windowIndex);
    settings.beginGroup(key);

    settings.setValue("geometry", saveGeometry());
    settings.setValue("mode", static_cast<int>(m_pMemoryWidget->GetMode()));
    settings.endGroup();
}

void MemoryViewWidget::requestAddress(int windowIndex, bool isMemory, uint32_t address)
{
    if (!isMemory)
        return;

    if (windowIndex != m_windowIndex)
        return;

    m_pMemoryWidget->SetLock(false);
    m_pMemoryWidget->SetAddress(std::to_string(address));
    m_pLockCheckBox->setChecked(false);
    setVisible(true);
}

void MemoryViewWidget::textEditChangedSlot()
{
    m_pMemoryWidget->SetAddress(m_pLineEdit->text().toStdString());
}

void MemoryViewWidget::lockChangedSlot()
{
    m_pMemoryWidget->SetLock(m_pLockCheckBox->isChecked());
}

void MemoryViewWidget::modeComboBoxChanged(int index)
{
    m_pMemoryWidget->SetMode((MemoryWidget::Mode)index);
    //m_pTableView->resizeColumnToContents(MemoryWidget::kColData);
}
