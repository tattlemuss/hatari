#ifndef MEMORYVIEWWIDGET_H
#define MEMORYVIEWWIDGET_H

#include <QDockWidget>
#include <QTableView>
#include "../models/memory.h"

class Session;
class TargetModel;
class Dispatcher;
class QComboBox;
class QCheckBox;

class MemoryWidget : public QWidget
{
    Q_OBJECT
public:
    enum Column
    {
        kColAddress,
        kColData,
        kColAscii,
        kColCount
    };

    enum Mode
    {
        kModeByte,
        kModeWord,
        kModeLong
    };

    MemoryWidget(QWidget* parent, Session* pSession, int windowIndex);

    uint32_t GetRowCount() const { return m_rowCount; }
    Mode GetMode() const { return m_mode; }

    // returns false if expression is invalid
    bool SetAddress(std::string expression);
    void SetLock(bool locked);
    void SetMode(Mode mode);

public slots:
    void memoryChangedSlot(int memorySlot, uint64_t commandId);
    void startStopChangedSlot();
    void connectChangedSlot();
    void otherMemoryChangedSlot(uint32_t address, uint32_t size);
    void settingsChangedSlot();

protected:
    virtual void paintEvent(QPaintEvent*);
    virtual void keyPressEvent(QKeyEvent*);
    virtual void mousePressEvent(QMouseEvent *event);
private:
    void MoveUp();
    void MoveDown();
    void MoveLeft();
    void MoveRight();
    void PageUp();
    void PageDown();
    void EditKey(uint8_t val);

    void SetAddress(uint32_t address);
    void RequestMemory();
    void RecalcText();
    void resizeEvent(QResizeEvent *event);
    void RecalcRowCount();

    void UpdateFont();
    int GetAddrX() const;
    // Get the x-coord of the hex-character at cursor column
    int GetHexCharX(int column) const;
    int GetAsciiCharX(int column) const;

    // Convert from row ID to a pixel Y (top pixel in the drawn row)
    int GetPixelFromRow(int row) const;

    // Convert from pixel Y to a row ID
    int GetRowFromPixel(int y) const;

    void GetCursorInfo(uint32_t& address, bool& bottomNybble);
    void SetRowCount(int rowCount);

    Session*        m_pSession;
    TargetModel*    m_pTargetModel;
    Dispatcher*     m_pDispatcher;

    // These are taken at the same time. Is there a race condition...?
    struct Row
    {
        uint32_t m_address;

        QVector<uint8_t> m_rawBytes;
        QVector<bool> m_byteChanged;
        QString m_hexText;
        QString m_asciiText;
    };

    QVector<Row> m_rows;
    // Positions of each column (need to multiply by m_charWidth for pixel position)
    QVector<int32_t> m_columnPositions;

    std::string m_addressExpression;
    bool        m_isLocked;
    uint32_t    m_address;

    int         m_bytesPerRow;
    Mode        m_mode;

    int         m_rowCount;
    uint64_t    m_requestId;
    int         m_windowIndex;        // e.g. "memory 0", "memory 1"
    MemorySlot  m_memSlot;

    Memory      m_previousMemory;       // Copy before we restarted the CPU

    // Cursor
    int         m_cursorRow;
    int         m_cursorCol;

    // rendering info
    int         m_charWidth;            // font width in pixels
    int         m_lineHeight;           // font height in pixels
    QFont       m_monoFont;
};

class MemoryWindow : public QDockWidget
{
    Q_OBJECT
public:
    MemoryWindow(QWidget *parent, Session* pSession, int windowIndex);

    // Grab focus and point to the main widget
    void keyFocus();

    void loadSettings();
    void saveSettings();

public slots:
    void requestAddress(int windowIndex, bool isMemory, uint32_t address);

    void textEditChangedSlot();
    void lockChangedSlot();
    void modeComboBoxChanged(int index);

private:
    QLineEdit*          m_pLineEdit;
    QComboBox*          m_pComboBox;
    QCheckBox*          m_pLockCheckBox;
    MemoryWidget*       m_pMemoryWidget;

    Session*            m_pSession;
    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;
    QAbstractItemModel* m_pSymbolTableModel;
    int                 m_windowIndex;
};

#endif // MEMORYVIEWWIDGET_H
