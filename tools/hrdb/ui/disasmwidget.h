#ifndef DISASMWINDOW_H
#define DISASMWINDOW_H

#include <QDockWidget>
#include <QTableView>
#include <QMenu>
#include "../models/disassembler.h"
#include "../models/breakpoint.h"
#include "../models/memory.h"

class Session;
class TargetModel;
class Dispatcher;
class QCheckBox;
class QPaintEvent;
class QSettings;

class DisasmWidget : public QWidget
{
    Q_OBJECT
public:
    DisasmWidget(QWidget * parent, Session* m_pSession, int windowIndex);
    virtual ~DisasmWidget() override;

    // "The model emits signals to indicate changes. For example, dataChanged() is emitted whenever items of data made available by the model are changed"
    // So I expect we can emit that if we see the target has changed

    uint32_t GetAddress() const { return m_logicalAddr; }
    int GetRowCount() const     { return m_rowCount; }
    bool GetFollowPC() const    { return m_bFollowPC; }
    bool GetShowHex() const     { return m_bShowHex; }
    bool GetInstructionAddr(int row, uint32_t& addr) const;
    bool GetEA(int row, int operandIndex, uint32_t &addr);

    bool SetAddress(std::string addr);
    void MoveUp();
    void MoveDown();
    void PageUp();
    void PageDown();
    void RunToRow(int row);
    void ToggleBreakpoint(int row);
    void NopRow(int row);
    void SetRowCount(int count);
    void SetShowHex(bool show);
    void SetFollowPC(bool follow);
public slots:
signals:
    void addressChanged(uint64_t addr);

private slots:
    void startStopChangedSlot();
    void connectChangedSlot();
    void memoryChangedSlot(int memorySlot, uint64_t commandId);
    void breakpointsChangedSlot(uint64_t commandId);
    void symbolTableChangedSlot(uint64_t commandId);
    void otherMemoryChangedSlot(uint32_t address, uint32_t size);

    void runToCursor();
    void toggleBreakpoint();

private:
    virtual void paintEvent(QPaintEvent* ev) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void resizeEvent(QResizeEvent *event) override;
    virtual bool event(QEvent *ev) override;

    void SetAddress(uint32_t addr);
    void RequestMemory();
    void CalcDisasm();
    void CalcOpAddresses();
    void printEA(const operand &op, const Registers &regs, uint32_t address, QTextStream &ref) const;

    // Cached data when the up-to-date request comes through
    Memory       m_memory;

    struct OpAddresses
    {
        bool valid[2];
        uint32_t address[2];
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
        QString     disasm;
        QString     comments;
    };
    QVector<RowText>    m_rowTexts;

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
    void nopRightClick();

    // Callbacks when the matching entry of m_pShowMemMenus is chosen
    void showMemMenu0Shown();
    void showMemMenu1Shown();
    void showMemMenu2Shown();

    // Callbacks when "show in Memory X" etc is selected
    void disasmViewTrigger(int windowIndex);
    void memoryViewTrigger(int windowIndex);
    void settingsChangedSlot();

    // Layout functions
    void RecalcRowCount();
    void UpdateFont();
    void RecalcColums();

    // Convert from row ID to a pixel Y (top pixel in the drawn row)
    int GetPixelFromRow(int row) const;

    // Convert from pixel Y to a row ID
    int GetRowFromPixel(int y) const;

    Session*              m_pSession;
    TargetModel*          m_pTargetModel;   // for inter-window comms
    Dispatcher*           m_pDispatcher;

    // Actions - top level rightclick
    QAction*              m_pRunUntilAction;
    QAction*              m_pBreakpointAction;
    QMenu*                m_pEditMenu;        // "edit this instruction" menu
    QAction*              m_pNopAction;

    // "Show memory for $x" top-level menus:
    // Show Instruction
    // Show EA 0
    // Show EA 1
    QMenu *               m_pShowMemMenus[3];
    uint32_t              m_showMenuAddresses[3];
    uint32_t              m_rightClickActiveAddress;    // One of m_showMenuAddresses[] selected by triggering the menu

    // Each top-level action has 4 sub-actions
    // 0 show in Disasm 1
    // 1 show in Disasm 2
    // 2 show in Memory 1
    // 3 show in Memory 2
    // These actions are shared by *all* menus in m_pShowMemMenus.
    // The "active" address, m_rightClickActiveAddress, is set when the relevant
    // m_pShowMemMenus menu is about to be shown.
    QAction*              m_pShowDisasmWindowActions[kNumDisasmViews];
    QAction*              m_pShowMemoryWindowActions[kNumMemoryViews];

    // Column layout
    bool                  m_bShowHex;

    enum Column
    {
        kSymbol,
        kAddress,
        kPC,
        kBreakpoint,
        kHex,
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
    void requestAddress(int windowIndex, bool isMemory, uint32_t address);


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

private:

    void UpdateTextBox();

    QLineEdit*      m_pLineEdit;
    QCheckBox*      m_pShowHex;
    QCheckBox*      m_pFollowPC;
    Session*        m_pSession;
    DisasmWidget*   m_pDisasmWidget;
    TargetModel*    m_pTargetModel;
    Dispatcher*     m_pDispatcher;
    QAbstractItemModel* m_pSymbolTableModel;        // used for autocomplete

    int             m_windowIndex;
};

#endif // DISASMWINDOW_H
