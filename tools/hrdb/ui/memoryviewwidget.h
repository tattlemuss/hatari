#ifndef MEMORYVIEWWIDGET_H
#define MEMORYVIEWWIDGET_H

#include <QDockWidget>
#include <QAbstractItemModel>
#include "savebindialog.h"
#include "searchdialog.h"
#include "showaddressactions.h"
#include "../models/memory.h"
#include "../models/session.h"

class TargetModel;
class SymbolTableModel;
class Dispatcher;
class QComboBox;
class QCheckBox;
class ElidedLabel;

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

    enum SizeMode
    {
        kModeByte,
        kModeWord,
        kModeLong
    };

    enum WidthMode
    {
        k4,
        k8,
        k16,
        k32,
        k64,
        kAuto = 100,  // Not currently supported, but here for future expansion
    };

    struct CursorInfo
    {
        MemSpace m_cursorSpace;
        uint32_t m_cursorAddress;
        bool     m_isCursorValid;

        uint32_t m_startAddress;     // addr of top-left entry
        uint32_t m_sizeInBytes;
    };

    MemoryWidget(QWidget* parent, Session* pSession, int windowIndex, QAction* pSearchAction, QAction* pSaveAction);
    virtual ~MemoryWidget();

    uint32_t GetRowCount() const { return m_rowCount; }
    SizeMode GetSizeMode() const { return m_sizeMode; }
    WidthMode GetWidthMode() const { return m_widthMode; }
    MemSpace GetSpace() const { return m_address.space; }

    const CursorInfo& GetCursorInfo() const { return m_cursorInfo; }
    bool IsLocked() const { return m_isLocked; }

    // Set only the new space (via UI combo)
    // Does re-request memory.
    void SetSpace(MemSpace space);

    // Set space+address, usually a request from another window
    // Does re-request memory.
    void SetAddress(MemAddr full);

    // Checks expression validity
    bool CanSetExpression(std::string expression) const;

    // Set the text expression used as the address (not including space)
    // returns false if expression is invalid.
    // Requests memory.
    bool SetExpression(std::string expression);

    // Requests memory.
    void SetSearchResultAddress(MemAddr addr);

    // Requests memory if the lock is turned on!
    void SetLock(bool locked);

    // Does not request memory.
    void SetSizeMode(SizeMode mode);

    // Requests memory.
    void SetWidthMode(WidthMode widthMode);

signals:
    // Flagged when cursor position or memory under cursor changes
    void cursorChangedSignal();
    void spaceChangedSignal();
    void lockChangedSignal();
protected:
    virtual void paintEvent(QPaintEvent*) override;
    virtual void keyPressEvent(QKeyEvent*) override;
    virtual void mousePressEvent(QMouseEvent *event) override;
    virtual void wheelEvent(QWheelEvent* event) override;
    virtual void contextMenuEvent(QContextMenuEvent *event) override;
    virtual void resizeEvent(QResizeEvent *event) override;
    virtual bool event(QEvent *event) override;

private:
    struct ViewState
    {
        MemAddr address;
        int rowCount;
        int bytesPerRow;
        SizeMode sizeMode;
    };

    void memoryChanged(int memorySlot, uint64_t commandId);
    void startStopChanged();
    void connectChanged();
    void registersChanged();
    void otherMemoryChanged(uint32_t address, uint32_t size);
    void symbolTableChanged();
    void settingsChanged();
    void configChanged();

    void CursorUp();
    void CursorDown();
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

    // Update the space in m_address and potentially change column layout.
    // Does not request memory.
    void SetSpaceInternal(MemSpace space);

    // Update space and address, and potentially change column layout
    // Does not request memory.
    void SetAddressInternal(MemAddr address);

    // Fetch memory in m_address
    void RequestMemory(CursorMode moveCursor);

    // Is we are locked to an expression recalc m_address.
    // Does not request memory.
    void RecalcLockedExpression();

    // Parse a CPU or DSP expression, depending on mode
    bool ParseExpression(bool isCpu, const std::string& expr, uint32_t& result) const;

    // Rearrange the layout to match width/format decisions.
    // Recalculates text.
    void RecalcColumnLayout();
    void RecalcText();

    // Fit visible rows.
    // Recalculates text.
    void RecalcRowCount();

    // Calculate number of bytes to fit current window width.
    // Does not request memory.
    void RecalcAutoRowWidth();

    // Update cursor data and emit cursorChangedSignal
    void RecalcCursorInfo();

    QString CalcMouseoverText(int mouseX, int mouseY);

    void UpdateFont();
    int GetAddrX() const;

    // Get the x-coord of the character at cursor column
    int GetPixelFromCol(int column) const;

    // Convert from row ID to a pixel Y (top pixel in the drawn row)
    int GetPixelFromRow(int row) const;

    // Convert from pixel Y to a row ID
    int GetRowFromPixel(int y) const;

    // Create context menu at the cursor pos
    void KeyboardContextMenu();

    // Create a context menu based on a grid position
    void ContextMenu(int row, int col, QPoint globalPos);

    // Find a valid entry under the pixel
    bool CalcRowColFromMouse(int x, int y, int& row, int& col);

    void SetRowCount(int rowCount);

    uint32_t CalcAddrOffset(MemAddr addr, int row, int col) const;
    uint32_t CalcAddress(MemAddr addr, int row, int col) const;

    Session*        m_pSession;
    TargetModel*    m_pTargetModel;
    Dispatcher*     m_pDispatcher;

    enum ColumnType
    {
        kSpace,
        kTopNybble,
        kBottomNybble,
        kASCII,
        kInvalid            // for when memory is not available (yet)
    } type;

    // These are taken at the same time. Is there a race condition...?
    struct RowData
    {
        QVector<ColumnType> m_types;
        QVector<bool> m_byteChanged;
        QVector<int> m_symbolId;
        QString m_text;
    };
    // This is the visible UI information
    QVector<RowData> m_rows;

    std::string m_addressExpression;
    bool        m_isLocked;

    // This block effectively stores the whole current UI state.
    // If anything changes
    MemAddr     m_address;
    WidthMode   m_widthMode;
    SizeMode    m_sizeMode;

    uint32_t    m_lastAddresses[MEM_SPACE_MAX];

    // These are calculated from m_widthMode
    int         m_bytesPerRow;
    int         m_hasAscii;

    int         m_rowCount;
    int         m_cursorRow;
    int         m_cursorCol;

    // This describes the layout of characters in each column of the grid.
    // It updates based on width mode and bytes-per-row.
    struct ColInfo
    {
        ColumnType type;         // what sort of entry this is
        uint32_t byteOffset;     // offset into memory when type != kSpace
    };
    QVector<ColInfo> m_columnMap;

    // Memory requests
    uint64_t    m_requestId;
    CursorMode  m_requestCursorMode;

    int         m_windowIndex;          // e.g. "memory 0", "memory 1"
    MemorySlot  m_memSlot;

    Memory      m_currentMemory;        // Latest copy available
    Memory      m_previousMemory;       // Copy before we restarted the CPU

    CursorInfo  m_cursorInfo;

    // rendering info
    int         m_charWidth;            // font width in pixels
    int         m_lineHeight;           // font height in pixels
    QFont       m_monoFont;

    // Menu actions
    ShowAddressMenu     m_showThisAddressMenu;   // 0 == "this address", 1 == "referenced address"
    ShowAddressMenu     m_showPtrAddressMenu[3];
    QAction*            m_pSearchAction;
    QAction*            m_pSaveAction;

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
    void requestAddress(Session::WindowType type, int windowIndex, int memorySpace, uint32_t address);

    void returnPressedSlot();
    void cursorChangedSlot();
    void textEditedSlot();
    void lockChangedSlot();
    void spaceComboBoxChangedSlot(int index);
    void sizeModeComboBoxChangedSlot(int index);
    void widthComboBoxChangedSlot(int index);
    void findClickedSlot();
    void saveBinClickedSlot();
    void nextClickedSlot();
    void gotoClickedSlot();
    void lockClickedSlot();
    void searchResultsSlot(uint64_t responseId);
    void symbolTableChangedSlot(uint64_t responseId);
    void syncUiElements();

private:
    QLineEdit*          m_pAddressEdit;
    QComboBox*          m_pSpaceComboBox;

    QComboBox*          m_pSizeModeComboBox;
    QComboBox*          m_pWidthComboBox;
    QCheckBox*          m_pLockCheckBox;
    ElidedLabel*        m_pCursorInfoLabel;
    MemoryWidget*       m_pMemoryWidget;

    Session*            m_pSession;
    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;
    SymbolTableModel*   m_pSymbolTableModel;
    int                 m_windowIndex;

    SearchSettings      m_searchSettings;
    uint64_t            m_searchRequestId;
    SaveBinSettings     m_saveBinSettings;
};

#endif // MEMORYVIEWWIDGET_H
