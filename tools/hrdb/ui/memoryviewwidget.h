#ifndef MEMORYVIEWWIDGET_H
#define MEMORYVIEWWIDGET_H

#include <QDockWidget>
#include <QTableView>
#include "searchdialog.h"
#include "showaddressactions.h"
#include "../models/memory.h"
#include "../models/session.h"

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
    virtual ~MemoryWidget();

    uint32_t GetRowCount() const { return m_rowCount; }
    Mode GetMode() const { return m_mode; }
    bool GetAddressAtCursor(uint32_t &address) const;

    // Checks expression validity
    bool CanSetExpression(std::string expression) const;
    // Set the text expression used as the address.
    // returns false if expression is invalid
    bool SetExpression(std::string expression);
    void SetSearchResultAddress(uint32_t addr);

    void SetLock(bool locked);
    void SetMode(Mode mode);

protected:
    virtual void paintEvent(QPaintEvent*) override;
    virtual void keyPressEvent(QKeyEvent*) override;
    virtual void mousePressEvent(QMouseEvent *event) override;
    virtual void wheelEvent(QWheelEvent* event) override;
    virtual void contextMenuEvent(QContextMenuEvent *event) override;
    virtual void resizeEvent(QResizeEvent *event) override;
    virtual bool event(QEvent *event) override;

private:
    void memoryChanged(int memorySlot, uint64_t commandId);
    void startStopChanged();
    void connectChanged();
    void registersChanged();
    void otherMemoryChanged(uint32_t address, uint32_t size);
    void symbolTableChanged();
    void settingsChanged();

    void MoveUp();
    void MoveDown();
    void MoveLeft();
    void MoveRight();
    void PageUp();
    void PageDown();
    void MouseWheelUp();
    void MouseWheelDown();

    // Change top address by the relative number of "bytes" stated.
    // Called by all the movement functions
    void MoveRelative(int32_t bytes);

    // Returns true if key was used (so we know not to block it)
    bool EditKey(char key);
    char IsEditKey(const QKeyEvent *event);

    enum CursorMode
    {
        kNoMoveCursor,
        kMoveCursor
    };

    void SetAddress(uint32_t address, CursorMode moveCursor);
    void RequestMemory(CursorMode moveCursor);

    // Is we are locked to an expression recalc m_address
    void RecalcLockedExpression();
    void RecalcText();
    void RecalcRowCount();

    QString CalcMouseoverText(int mouseX, int mouseY);

    void UpdateFont();
    int GetAddrX() const;

    // Get the x-coord of the character at cursor column
    int GetPixelFromCol(int column) const;

    // Convert from row ID to a pixel Y (top pixel in the drawn row)
    int GetPixelFromRow(int row) const;

    // Convert from pixel Y to a row ID
    int GetRowFromPixel(int y) const;

    // Find a valid entry under the pixel
    bool FindInfo(int x, int y, int& row, int& col);


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
        QVector<int> m_symbolId;
        QString m_text;
    };

    QVector<Row> m_rows;
    struct ColInfo
    {
        enum Type
        {
            kSpace,
            kTopNybble,
            kBottomNybble,
            kASCII
        } type;
        uint32_t byteOffset;     // offset into memory
    };

    QVector<ColInfo> m_columnMap;

    std::string m_addressExpression;
    bool        m_isLocked;

    int         m_bytesPerRow;
    Mode        m_mode;

    int         m_rowCount;

    // Memory requests
    uint32_t    m_address;
    uint64_t    m_requestId;
    CursorMode  m_requestCursorMode;

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

    // Menu actions
    ShowAddressMenu     m_showAddressMenus[2];   // 0 == "this address", 1 == "referenced address"

    // Mouse wheel
    float               m_wheelAngleDelta;
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
    void requestAddress(Session::WindowType type, int windowIndex, uint32_t address);

    void returnPressedSlot();
    void textEditedSlot();
    void lockChangedSlot();
    void modeComboBoxChanged(int index);
    void findClickedSlot();
    void nextClickedSlot();
    void gotoClickedSlot();
    void lockClickedSlot();
    void searchResultsSlot(uint64_t responseId);

private:
    QLineEdit*          m_pAddressEdit;
    QComboBox*          m_pComboBox;
    QCheckBox*          m_pLockCheckBox;
    MemoryWidget*       m_pMemoryWidget;

    Session*            m_pSession;
    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;
    QAbstractItemModel* m_pSymbolTableModel;
    int                 m_windowIndex;

    SearchSettings      m_searchSettings;
    uint64_t            m_searchRequestId;
};

#endif // MEMORYVIEWWIDGET_H
