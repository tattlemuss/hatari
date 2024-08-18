#ifndef REGISTERWIDGET_H
#define REGISTERWIDGET_H
#include <QWidget>

#include "../models/targetmodel.h"
#include "../models/disassembler.h"
#include "../models/disassembler56.h"
#include "showaddressactions.h"

class Dispatcher;
class Session;

class RegisterWidget : public QWidget
{
    Q_OBJECT
public:
    RegisterWidget(QWidget* parent, Session* pSession);
    virtual ~RegisterWidget() override;

    void saveSettings();
    void loadSettings();

protected:
    virtual void paintEvent(QPaintEvent*) override;
    virtual void mouseMoveEvent(QMouseEvent *event) override;
    virtual void contextMenuEvent(QContextMenuEvent *event) override;
    virtual bool event(QEvent *event) override;

private:
    void connectChanged();
    void startStopChanged();
    void registersChanged(uint64_t commandId);
    void memoryChanged(int slot, uint64_t commandId);
    void symbolTableChanged(uint64_t commandId);
    void startStopDelayed(int running);
    void mainStateUpdated();

    void showCpuTriggered();
    void showDspTriggered();

    void settingsChanged();

    void PopulateRegisters();
    void UpdateFont();

    // Tokens etc
    enum TokenType
    {
        kRegister,
        kDspRegister,
        kSymbol,                // aka an arbitrary "address"
        kStatusRegisterBit,
        kDspStatusRegisterBit,
        kCACRBit,
        kDspOMRBit,
        kNone,
    };

    enum TokenColour
    {
        kNormal,
        kChanged,
        kInactive,
        kCode
    };

    struct Token
    {
        int x;
        int y;
        QString text;

        TokenType type;
        uint32_t subIndex;      // subIndex e.g "4" for D4, 0x12345 for symbol address, bitnumber for SR field
        TokenColour colour;     // how to draw it
        bool invert;            // swap back + front
        QRectF rect;            // bounding rectangle, updated when rendered
    };

    QString FindSymbol(uint32_t addr);

    int AddToken(int x, int y, QString text, TokenType type, uint32_t subIndex = 0, TokenColour colour = TokenColour::kNormal,
                 bool invert = false);
    int AddReg16(int x, int y, uint32_t regIndex);
    int AddReg32(int x, int y, uint32_t regIndex);
    int AddDspReg24(int x, int y, uint32_t regIndex);
    int AddDspReg16(int x, int y, uint32_t regIndex);
    int AddDspReg8(int x, int y, uint32_t regIndex);

    int AddSRBit(int x, int y, uint32_t bit, const char *pName);
    int AddDspSRBit(int x, int y, uint32_t bit, const char *pName);
    int AddDspOMRBit(int x, int y, uint32_t bit, const char* pName);
    int AddCACRBit(int x, int y, uint32_t bit, const char *pName);
    int AddSymbol(int x, int y, uint32_t address);

    QString GetTooltipText(const Token& token);
    void UpdateTokenUnderMouse();

    // Convert from row ID to a pixel Y (top pixel in the drawn row)
    int GetPixelFromRow(int row) const;

    // Convert from column ID to a pixel X (left pixel in the drawn col)
    int GetPixelFromCol(int col) const;

    // Convert from pixel Y to a row ID
    int GetRowFromPixel(int y) const;

    // UI Elements
    ShowAddressMenu             m_showAddressMenu;      // When right-clicking a register
    ShowAddressMenuDsp          m_showAddressMenuDsp;   // When right-clicking a register

    QMenu*                      m_pFilterMenu;
    QAction*                    m_pShowCpuAction;
    QAction*                    m_pShowDspAction;
    bool                        m_showCpu;
    bool                        m_showDsp;

    Session*                    m_pSession;
    Dispatcher*             	m_pDispatcher;
    TargetModel*                m_pTargetModel;

    // Shown data
    Registers                   m_currRegs;     // current regs
    Registers                   m_prevRegs;     // regs when PC started
    Disassembler::disassembly   m_disasm;

    DspRegisters                m_currDspRegs;
    DspRegisters                m_prevDspRegs;
    Disassembler56::disassembly m_disasmDsp;

    QVector<Token>              m_tokens;
    QVector<int>                m_rulers;

    // Mouse data
    QPointF                     m_mousePos;                  // last updated position
    int                         m_tokenUnderMouseIndex;      // -1 for none
    Token                       m_tokenUnderMouse;           // copy of relevant token (for menus etc)
    uint32_t                    m_addressUnderMouse;

    // Render info
    QFont                       m_monoFont;
    int                         m_yAscent;        // Font ascent (offset from top for drawing)
    int                         m_lineHeight;
    int                         m_charWidth;
};

#endif // REGISTERWIDGET_H
