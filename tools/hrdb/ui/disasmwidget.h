#ifndef DISASMWINDOW_H
#define DISASMWINDOW_H

#include <QDockWidget>
#include <QTableView>
#include <QMenu>
#include "../models/breakpoint.h"
#include "../models/disassembler.h"
#include "../models/memory.h"
#include "../models/session.h"
#include "showaddressactions.h"
#include "searchdialog.h"

class TargetModel;
class Dispatcher;
class QCheckBox;
class QPaintEvent;
class QSettings;
class SymbolTableModel;

class DisasmWidget : public QWidget
{
    Q_OBJECT
public:
    DisasmWidget(QWidget * parent, Session* m_pSession, int windowIndex, QAction* pSearchAction);
    virtual ~DisasmWidget() override;

    uint32_t GetAddress() const { return m_logicalAddr; }
    bool GetAddressAtCursor(uint32_t& addr) const;

    int GetRowCount() const     { return m_rowCount; }
    bool GetFollowPC() const    { return m_bFollowPC; }
    bool GetShowHex() const     { return m_bShowHex; }
    bool GetInstructionAddr(int row, uint32_t& addr) const;
    bool GetEA(int row, int operandIndex, uint32_t &addr);

    bool SetAddress(std::string addr);
    bool SetSearchResultAddress(uint32_t addr);
    void MoveUp();
    void MoveDown();

    void PageUp();
    void PageDown();

    void MouseScrollUp();
    void MouseScrollDown();

    void RunToRow(int row);
    void ToggleBreakpoint(int row);
    void SetPC(int row);
    void NopRow(int row);
    void SetRowCount(int count);
    void SetShowHex(bool show);
    void SetFollowPC(bool follow);
signals:
    void addressChanged(uint64_t addr);

private:
    void startStopChanged();
    void connectChanged();
    void memoryChanged(int memorySlot, uint64_t commandId);
    void breakpointsChanged(uint64_t commandId);
    void symbolTableChanged(uint64_t commandId);
    void otherMemoryChanged(uint32_t address, uint32_t size);
    void profileChanged();

    // From keyPressEvent
    void runToCursor();
    void toggleBreakpoint();

    virtual void paintEvent(QPaintEvent* ev) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void wheelEvent(QWheelEvent* event) override;
    virtual void resizeEvent(QResizeEvent *event) override;
    virtual bool event(QEvent *ev) override;

    void SetAddress(uint32_t addr);
    void RequestMemory();
    void CalcDisasm();
    void CalcOpAddresses();
    void printEA(const hop68::operand &op, const Registers &regs, uint32_t address, QTextStream &ref) const;

    // Cached data when the up-to-date request comes through
    Memory       m_memory;

    struct OpAddresses
    {
        bool valid[2];
        uint32_t address[2];
        QString annotationText;     // Extra info like trap call name, line-A in future?
    };

    Disassembler::disassembly m_disasm;
    QVector<OpAddresses> m_opAddresses;
    struct RowText
    {
        QString     symbol;
        QString     address;

        bool        isPc;
        bool        isBreakpoint;

        QString     hex;
        QString     cycles;
        QString     disasm;
        QString     comments;

        int         branchTargetLine;       // -1 for no branch, or the ID of the target line
    };
    QVector<RowText>    m_rowTexts;

    struct Branch
    {
        int top() const { return std::min(start, stop);}
        int bottom() const { return std::max(start, stop);}
        int start;
        int stop;
        int depth;
        int type; // 0=normal, 1=top, 2=bottom
    };
    QVector<Branch>     m_branches;

    Breakpoints m_breakpoints;
    int         m_rowCount;

    // Address of the top line of text that was requested
    uint32_t m_requestedAddress;    // Most recent address requested

    uint32_t m_logicalAddr;         // Most recent address that can be shown
    uint64_t m_requestId;           // Most recent memory request
    bool     m_bFollowPC;

    int         m_windowIndex;
    MemorySlot  m_memSlot;
    QPixmap     m_breakpointPixmap;
    QPixmap     m_breakpointPcPixmap;
    QPixmap     m_pcPixmap;

    static const uint32_t kInvalid = 0xffffffff;

    virtual void contextMenuEvent(QContextMenuEvent *event) override;

    void runToCursorRightClick();
    void toggleBreakpointRightClick();
    void setPCRightClick();
    void nopRightClick();

    void settingsChangedSlot();

    // Layout functions
    void RecalcRowCount();
    void UpdateFont();
    void RecalcColums();

    // Convert from row ID to a pixel Y (top pixel in the drawn row)
    int GetPixelFromRow(int row) const;

    // Convert from pixel Y to a row ID
    int GetRowFromPixel(int y) const;

    void KeyboardContextMenu();
    void ContextMenu(int row, QPoint globalPos);

    Session*              m_pSession;
    TargetModel*          m_pTargetModel;   // for inter-window comms
    Dispatcher*           m_pDispatcher;

    // Actions - top level rightclick
    QAction*              m_pRunUntilAction;
    QAction*              m_pBreakpointAction;
    QAction*              m_pSetPcAction;
    QMenu*                m_pEditMenu;        // "edit this instruction" menu
    QAction*              m_pNopAction;

    QAction*              m_pSearchAction;    // Passed from parent window...

    // "Show memory for $x" top-level menus:
    // Show Instruction
    // Show EA 0
    // Show EA 1
    ShowAddressMenu       m_showAddressMenus[3];

    // Column layout
    bool                  m_bShowHex;

    enum Column
    {
        kSymbol,
        kAddress,
        kPC,
        kBreakpoint,
        kHex,
        kCycles,
        kDisasm,
        kComments,
        kNumColumns
    };
    int                   m_columnLeft[kNumColumns + 1];    // Left X position of each column. Include +1 in array for RHS

    // Remembers which row we right-clicked on
    int                   m_rightClickRow;

    // Selection state
    int                   m_cursorRow;
    int                   m_mouseRow;

    // rendering info
    int                   m_charWidth;            // font width in pixels
    int                   m_lineHeight;           // font height in pixels
    QFont                 m_monoFont;

    // Mouse wheel
    float                 m_wheelAngleDelta;
};


class DisasmWindow : public QDockWidget
{
    Q_OBJECT
public:
    DisasmWindow(QWidget *parent, Session* pSession, int windowIndex);

    // Grab focus and point to the main widget
    void keyFocus();

    void loadSettings();
    void saveSettings();

public slots:
    void requestAddress(Session::WindowType type, int windowIndex, uint32_t address);

protected:

protected slots:
    void keyDownPressed();
    void keyUpPressed();
    void keyPageDownPressed();
    void keyPageUpPressed();
    void returnPressedSlot();
    void textChangedSlot();

    void showHexClickedSlot();
    void followPCClickedSlot();

    void findClickedSlot();
    void nextClickedSlot();
    void gotoClickedSlot();
    void lockClickedSlot();
    void searchResultsSlot(uint64_t responseId);
    void symbolTableChangedSlot(uint64_t responseId);
private:

    void UpdateTextBox();

    QLineEdit*      m_pAddressEdit;
    QCheckBox*      m_pShowHex;
    QCheckBox*      m_pFollowPC;
    Session*        m_pSession;
    DisasmWidget*   m_pDisasmWidget;
    TargetModel*    m_pTargetModel;
    Dispatcher*     m_pDispatcher;
    SymbolTableModel* m_pSymbolTableModel;        // used for autocomplete

    int             m_windowIndex;

    SearchSettings      m_searchSettings;
    uint64_t            m_searchRequestId;
};

#endif // DISASMWINDOW_H
