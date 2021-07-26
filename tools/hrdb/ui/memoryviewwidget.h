#ifndef MEMORYVIEWWIDGET_H
#define MEMORYVIEWWIDGET_H

#include <QDockWidget>
#include <QTableView>
#include "../models/memory.h"

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

    MemoryWidget(QWidget* parent, TargetModel* pTargetModel, Dispatcher* pDispatcher, int windowIndex);

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

    void RecalcSizes();
    int GetAddrX() const;
    // Get the x-coord of the hex-character at cursor column
    int GetHexCharX(int column) const;
    int GetAsciiCharX() const;

    void GetCursorInfo(uint32_t& address, bool& bottomNybble);
    void SetRowCount(uint32_t rowCount);

    TargetModel*    m_pTargetModel;
    Dispatcher*     m_pDispatcher;

    // These are taken at the same time. Is there a race condition...?
    struct Row
    {
        uint32_t m_address;
        std::vector<uint8_t> m_rawBytes;
        QString m_hexText;
        QString m_asciiText;
    };

    std::vector<Row> m_rows;
    // Positions of each column (need to multiply by m_charWidth for pixel position)
    std::vector<uint32_t> m_columnPositions;

    std::string m_addressExpression;
    bool    m_isLocked;
    uint32_t m_address;

    uint32_t m_bytesPerRow;
    Mode     m_mode;

    uint32_t m_rowCount;
    uint64_t m_requestId;
    int      m_windowIndex;        // e.g. "memory 0", "memory 1"
    MemorySlot  m_memSlot;

    // Cursor
    int     m_cursorRow;
    int     m_cursorCol;


    // rendering info
    int     m_charWidth;            // font width in pixels
    int     m_lineHeight;           // font height in pixels
    QFont   monoFont;
};

#if 0
class MemoryTableView : public QTableView
{
    Q_OBJECT
public:
    MemoryTableView(QWidget* parent, MemoryViewTableModel* pModel, TargetModel* pTargetModel);

public slots:

protected:
    QModelIndex moveCursor(QAbstractItemView::CursorAction cursorAction, Qt::KeyboardModifiers modifiers);
private:
    // override -- this doesn't trigger at the start?
    virtual void resizeEvent(QResizeEvent*);
private slots:
    void RecalcRowCount();

private:
    MemoryViewTableModel*     m_pTableModel;

    // Remembers which row we right-clicked on
    int                   m_rightClickRow;
};
#endif

class MemoryViewWidget : public QDockWidget
{
    Q_OBJECT
public:
    MemoryViewWidget(QWidget *parent, TargetModel* pTargetModel, Dispatcher* m_pDispatcher, int windowIndex);

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
    QLineEdit*           m_pLineEdit;
    QComboBox*           m_pComboBox;
    QCheckBox*           m_pLockCheckBox;
    MemoryWidget*        m_pMemoryWidget;

    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;
    QAbstractItemModel* m_pSymbolTableModel;
    int                 m_windowIndex;
};

#endif // MEMORYVIEWWIDGET_H
