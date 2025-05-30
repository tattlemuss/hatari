#include "registerwidget.h"

#include <QPainter>
#include <QPen>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>

#include "../models/session.h"
#include "../hardware/tos.h"
#include "symboltext.h"
#include "qtversionwrapper.h"

static QString CreateNumberTooltip(uint32_t value, uint32_t prevValue)
{
    uint16_t word = value & 0xffff;
    uint16_t byte = value & 0xff;

    QString final;
    QTextStream ref(&final);

    if (value != prevValue)
    {
        ref << QString::asprintf("Previous value: $%x (%d)\n", prevValue, static_cast<int32_t>(prevValue));
        uint32_t delta = value - prevValue;
        ref << QString::asprintf("Difference from previous: $%x (%d)\n", delta, static_cast<int32_t>(delta));
    }

    if (value & 0x80000000)
        ref << QString::asprintf("Long: %u (%d)\n", value, static_cast<int32_t>(value));
    else
        ref << QString::asprintf("Long: %u\n", value);
    if (value & 0x8000)
        ref << QString::asprintf("Word: %u (%d)\n", word, static_cast<int16_t>(word));
    else
        ref << QString::asprintf("Word: %u\n", word);
    if (value & 0x80)
        ref << QString::asprintf("Byte: %u (%d)\n", byte, static_cast<int8_t>(byte));
    else
        ref << QString::asprintf("Byte: %u\n", byte);

    ref << "Binary: ";
    for (int bit = 31; bit >= 0; --bit)
        ref << ((value & (1U << bit)) ? "1" : "0");
    ref << "\n";

    ref << "ASCII \"";
    for (int bit = 3; bit >= 0; --bit)
    {
        unsigned char val = static_cast<unsigned char>(value >> (bit * 8));
        ref << ((val >= 32 && val < 128) ? QString::fromUtf8(reinterpret_cast<const char*>(&val), 1) : ".");
    }
    ref << "\"\n";

    return final;
}

static QString CreateSRTooltip(uint32_t srRegValue, uint32_t registerBit)
{
    uint32_t valSet = (srRegValue >> registerBit) & 1;
    return QString::asprintf("%s = %s", Registers::GetSRBitName(registerBit),
                             valSet ? "TRUE" : "False");
}

static QString CreateCACRTooltip(uint32_t cacrRegValue, uint32_t registerBit)
{
    uint32_t valSet = (cacrRegValue >> registerBit) & 1;
    return QString::asprintf("%s = %s", Registers::GetCACRBitName(registerBit),
                             valSet ? "TRUE" : "False");
}

static QString MakeBracket(QString str)
{
    return QString("(") + str + ")";
}

RegisterWidget::RegisterWidget(QWidget *parent, Session* pSession) :
    QWidget(parent),
    m_pSession(pSession),
    m_pDispatcher(pSession->m_pDispatcher),
    m_pTargetModel(pSession->m_pTargetModel),
    m_tokenUnderMouseIndex(-1)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Listen for target changes
    connect(m_pTargetModel, &TargetModel::startStopChangedSignal,        this, &RegisterWidget::startStopChanged);
    connect(m_pTargetModel, &TargetModel::registersChangedSignal,        this, &RegisterWidget::registersChanged);
    connect(m_pTargetModel, &TargetModel::connectChangedSignal,          this, &RegisterWidget::connectChanged);
    connect(m_pTargetModel, &TargetModel::memoryChangedSignal,           this, &RegisterWidget::memoryChanged);
    connect(m_pTargetModel, &TargetModel::symbolTableChangedSignal,      this, &RegisterWidget::symbolTableChanged);
    connect(m_pTargetModel, &TargetModel::startStopChangedSignalDelayed, this, &RegisterWidget::startStopDelayed);

    connect(m_pSession, &Session::settingsChanged,  this, &RegisterWidget::settingsChanged);
    connect(m_pSession, &Session::mainStateUpdated, this, &RegisterWidget::mainStateUpdated);
    setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    setMouseTracking(true);

    UpdateFont();
    // Handle non-connection at start
    PopulateRegisters();
}

RegisterWidget::~RegisterWidget()
{
}

void RegisterWidget::paintEvent(QPaintEvent * ev)
{
    QWidget::paintEvent(ev);

    QPainter painter(this);
    painter.setFont(m_monoFont);
    QFontMetrics info(painter.fontMetrics());
    const QPalette& pal = this->palette();

    const QBrush& br = pal.window().color();
    painter.fillRect(this->rect(), br);
    painter.setPen(QPen(pal.dark(), hasFocus() ? 6 : 2));
    painter.drawRect(this->rect());

    for (int i = 0; i < m_tokens.size(); ++i)
    {
        Token& tok = m_tokens[i];

        int x = Session::kWidgetBorderX + tok.x * m_charWidth;
        int y = GetPixelFromRow(tok.y);
        int w = info.horizontalAdvance(tok.text);
        int h = m_lineHeight;
        tok.rect.setRect(x, y, w, h);

        if (tok.colour == TokenColour::kNormal)
            painter.setPen(pal.text().color());
        else if (tok.colour == TokenColour::kChanged)
            painter.setPen(Qt::red);
        else if (tok.colour == TokenColour::kInactive)
            painter.setPen(pal.light().color());
        else if (tok.colour == TokenColour::kCode)
            painter.setPen(Qt::darkGreen);

        if (i == m_tokenUnderMouseIndex && tok.type != TokenType::kNone)
        {
            painter.setBrush(pal.highlight());
            painter.setPen(Qt::NoPen);
            painter.drawRect(tok.rect);
            painter.setPen(pal.highlightedText().color());
        }

        painter.drawText(x, m_yAscent + y, tok.text);
    }
}

void RegisterWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_mousePos = QTEVENT_GET_LOCAL_POS(event);
    UpdateTokenUnderMouse();
    QWidget::mouseMoveEvent(event);
    update();
}

void RegisterWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_tokenUnderMouseIndex == -1)
        return;

    // Right click menus are instantiated on demand, so we can
    // dynamically add to them
    QMenu menu(this);

    // Add the default actions
    QMenu* pAddressMenu = nullptr;
    if (m_tokenUnderMouse.type == TokenType::kRegister)
    {
        m_addressUnderMouse = m_currRegs.Get(m_tokenUnderMouse.subIndex);
        pAddressMenu = new QMenu("", &menu);
        pAddressMenu->setTitle(QString::asprintf("Address $%08x", m_addressUnderMouse));
    }
    else if (m_tokenUnderMouse.type == TokenType::kSymbol)
    {
        m_addressUnderMouse = m_tokenUnderMouse.subIndex;
        pAddressMenu = new QMenu("", &menu);
        pAddressMenu->setTitle(QString::asprintf("Address $%08x", m_addressUnderMouse));
    }

    if (pAddressMenu)
    {
        m_showAddressActions.addActionsToMenu(pAddressMenu);
        m_showAddressActions.setAddress(m_pSession, m_addressUnderMouse);
        menu.addMenu(pAddressMenu);

        // Run it
        menu.exec(event->globalPos());
    }
}

bool RegisterWidget::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {

        QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);
        int index = m_tokenUnderMouseIndex;
        QString text;
        if (index != -1)
            text = GetTooltipText(m_tokens[index]);

        if (text.size() != 0)
            QToolTip::showText(helpEvent->globalPos(), text);
        else
        {
            QToolTip::hideText();
            event->ignore();
        }
        return true;
    }
    else if (event->type() == QEvent::Leave)
    {
        m_tokenUnderMouseIndex = -1;
        update();
    }
    else if (event->type() == QEvent::Enter)
    {
        QEnterEvent* pEnterEvent = static_cast<QEnterEvent* >(event);
        m_mousePos = QTEVENT_GET_LOCAL_POS(pEnterEvent);
        UpdateTokenUnderMouse();
        update();
    }
    return QWidget::event(event);
}

void RegisterWidget::connectChanged()
{
    PopulateRegisters();
}

void RegisterWidget::startStopChanged()
{
    bool isRunning = m_pTargetModel->IsRunning();

    // Update text here
    if (isRunning)
    {
        // Update our previous values
        // TODO: this is not the ideal way to do this, since there
        // are timing issues. In future, parcel up the stopping updates so
        // that widget refreshes happen as one.
        m_prevRegs = m_pTargetModel->GetRegs();

        // Don't populate display here, since it causes colours to flash
    }
    else
    {
        // STOPPED
        // We don't do any requests here; the MainWindow does them.
        // We just listen for the callbacks.
    }
}

void RegisterWidget::startStopDelayed(int running)
{
    if (m_pTargetModel->IsConnected() && running &&
            !m_pSession->GetSettings().m_liveRefresh)
    {
        m_tokens.clear();
        m_tokenUnderMouseIndex = -1;
        AddToken(1, 1, tr("Running, Ctrl+R to break..."), TokenType::kNone, 0);
        update();
    }
}

void RegisterWidget::mainStateUpdated()
{
    // Disassemble the first instruction
    m_disasm.lines.clear();
    const Memory* pMem = m_pTargetModel->GetMemory(MemorySlot::kMainPC);
    if (!pMem)
        return;

    buffer_reader disasmBuf(pMem->GetData(), pMem->GetSize(), pMem->GetAddress());
    Disassembler::decode_buf(disasmBuf, m_disasm, m_pTargetModel->GetDisasmSettings(), pMem->GetAddress(), 2);

    PopulateRegisters();
    update();
}

void RegisterWidget::settingsChanged()
{
    UpdateFont();
    PopulateRegisters();
    update();
}

void RegisterWidget::registersChanged(uint64_t /*commandId*/)
{
    // Update text here
    //PopulateRegisters();
}

void RegisterWidget::memoryChanged(int slot, uint64_t /*commandId*/)
{
    if (slot != MemorySlot::kMainPC)
        return;

    // This will be done when all main state updates
//    PopulateRegisters();
}

void RegisterWidget::symbolTableChanged(uint64_t /*commandId*/)
{
    PopulateRegisters();
}

void RegisterWidget::PopulateRegisters()
{
    m_tokens.clear();
    if (!m_pTargetModel->IsConnected())
    {
        m_tokenUnderMouseIndex = -1;
        AddToken(1, 1, tr("Not connected."), TokenType::kNone, 0);
        update();
        return;
    }

    // Precalc EAs
    const Registers& regs = m_pTargetModel->GetRegs();
    // Build up the text area
    m_currRegs = m_pTargetModel->GetRegs();

    // Row 0 -- PC, and symbol if applicable
    int row = 0;

    AddReg32(0, row, Registers::PC, m_prevRegs, m_currRegs);
    QString sym = FindSymbol(GET_REG(m_currRegs, PC) & 0xffffff);
    if (sym.size() != 0)
        AddToken(14, row, MakeBracket(sym), TokenType::kSymbol, GET_REG(m_currRegs, PC));

    row += 2;

    // Row 1 -- instruction and analysis
    if (m_disasm.lines.size() > 0)
    {
        QString disasmText = ">> ";
        QTextStream ref(&disasmText);

        const instruction& inst = m_disasm.lines[0].inst;
        Disassembler::print_terse(inst, m_disasm.lines[0].address, ref,m_pSession->GetSettings().m_bDisassHexNumerics);

        bool branchTaken;
        if (DisAnalyse::isBranch(inst, m_currRegs, branchTaken))
        {
            if (branchTaken)
                ref << " [TAKEN]";
            else
                ref << " [NOT TAKEN]";
        }

        int col = AddToken(0, row, disasmText, TokenType::kNone, 0, TokenColour::kCode) + 5;

        // Comments
        if (m_disasm.lines.size() != 0)
        {
            int i = 0;
            const instruction& inst = m_disasm.lines[i].inst;
            bool prevValid = false;
            for (int opIndex = 0; opIndex < 2; ++opIndex)
            {
                const operand& op = opIndex == 0 ? inst.op0 : inst.op1;
                if (op.type != OpType::INVALID)
                {
                    QString eaText = "";
                    QTextStream eaRef(&eaText);

                    // Separate info
                    if (prevValid)
                        col = AddToken(col, row, " | ", TokenType::kNone, 0, TokenColour::kNormal) + 1;

                    // Operand values
                    prevValid = false;
                    switch (op.type)
                    {
                    case OpType::D_DIRECT:
                        eaRef << QString::asprintf("D%d=$%x", op.d_register.reg, regs.GetDReg(op.d_register.reg));
                        col = AddToken(col, row, eaText, TokenType::kRegister, Registers::D0 + op.d_register.reg, TokenColour::kCode) + 1;
                        prevValid = true;
                        break;
                    case OpType::A_DIRECT:
                        eaRef << QString::asprintf("A%d=$%x", op.a_register.reg, regs.GetAReg(op.a_register.reg));
                        col = AddToken(col, row, eaText, TokenType::kRegister, Registers::A0 + op.a_register.reg, TokenColour::kCode) + 1;
                        prevValid = true;
                        break;
                    case OpType::SR:
                        eaRef << QString::asprintf("SR=$%x", regs.Get(Registers::SR));
                        col = AddToken(col, row, eaText, TokenType::kRegister, Registers::SR, TokenColour::kCode) + 1;
                        prevValid = true;
                        break;
                    case OpType::USP:
                        eaRef << QString::asprintf("USP=$%x", regs.Get(Registers::USP));
                        col = AddToken(col, row, eaText, TokenType::kRegister, Registers::USP, TokenColour::kCode) + 1;
                        prevValid = true;
                        break;
                    case OpType::CCR:
                        eaRef << QString::asprintf("CCR=$%x", regs.Get(Registers::SR));
                        // Todo: can we fix this?
                        col = AddToken(col, row, eaText, TokenType::kRegister, Registers::SR, TokenColour::kCode) + 1;
                        prevValid = true;
                        break;
                    default:
                        break;
                    }

                    uint32_t finalEA;
                    bool valid = Disassembler::calc_fixed_ea(op, true, regs, m_disasm.lines[i].address, finalEA);
                    if (valid)
                    {
                        // Show the calculated EA
                        QString eaAddrText = QString::asprintf("$%x", finalEA);
                        QString sym = FindSymbol(finalEA & 0xffffff);
                        if (sym.size() != 0)
                            eaAddrText = eaAddrText + " (" + sym + ")";
                        col = AddToken(col, row, eaAddrText, TokenType::kSymbol, finalEA, TokenColour::kCode) + 1;
                        prevValid = true;
                    }
                }
            }
            // TOS calls
            if (regs.m_value[Registers::GemdosOpcode] != 0xffff)
                col = AddToken(col, row, GetTrapAnnotation(1, regs.m_value[Registers::GemdosOpcode]), TokenType::kNone, 0, TokenColour::kChanged);
            else if (regs.m_value[Registers::BiosOpcode] != 0xffff)
                col = AddToken(col, row, GetTrapAnnotation(13, regs.m_value[Registers::BiosOpcode]), TokenType::kNone, 0, TokenColour::kChanged);
            else if (regs.m_value[Registers::XbiosOpcode] != 0xffff)
                col = AddToken(col, row, GetTrapAnnotation(14, regs.m_value[Registers::XbiosOpcode]), TokenType::kNone, 0, TokenColour::kChanged);
        }

        // Add EA analysis
//        ref << "   " << eaText;
    }

    ++row;

    ++row;
    AddReg16(1, row, Registers::SR, m_prevRegs, m_currRegs);
    AddSRBit(10, row, m_prevRegs, m_currRegs, Registers::SRBits::kTrace1, "T");
    AddSRBit(14, row, m_prevRegs, m_currRegs, Registers::SRBits::kSupervisor, "S");

    AddSRBit(19, row, m_prevRegs, m_currRegs, Registers::SRBits::kX, "X");
    AddSRBit(23, row, m_prevRegs, m_currRegs, Registers::SRBits::kN, "N");
    AddSRBit(27, row, m_prevRegs, m_currRegs, Registers::SRBits::kZ, "Z");
    AddSRBit(31, row, m_prevRegs, m_currRegs, Registers::SRBits::kV, "V");
    AddSRBit(35, row, m_prevRegs, m_currRegs, Registers::SRBits::kC, "C");
    QString iplLevel = QString::asprintf("IPL=%u", (m_currRegs.m_value[Registers::SR] >> 8 & 0x7));
    AddToken(39, row, iplLevel, TokenType::kNone);
    row += 1;

    uint32_t ex = GET_REG(m_currRegs, EX);
    if (ex != 0)
        AddToken(4, row, QString::asprintf("EXCEPTION: %s", ExceptionMask::GetName(ex)), TokenType::kNone, 0, TokenColour::kChanged);

    // D-regs // A-regs
    row++;
    for (uint32_t reg = 0; reg < 8; ++reg)
    {
        AddReg32(2, row, Registers::D0 + reg, m_prevRegs, m_currRegs);

        AddReg32(17, row, Registers::A0 + reg, m_prevRegs, m_currRegs); AddSymbol(30, row, m_currRegs.m_value[Registers::A0 + reg]);
        row++;
    }
    AddReg32(16, row, Registers::ISP, m_prevRegs, m_currRegs); AddSymbol(30, row, m_currRegs.m_value[Registers::ISP]);
    row++;
    AddReg32(16, row, Registers::USP, m_prevRegs, m_currRegs); AddSymbol(30, row, m_currRegs.m_value[Registers::USP]);
    if (m_pTargetModel->GetCpuLevel() >= TargetModel::CpuLevel::kCpuLevel68020)
    {
        row++;
        AddReg32(16, row, Registers::MSP, m_prevRegs, m_currRegs); AddSymbol(30, row, m_currRegs.m_value[Registers::MSP]);
    }
    row++;
    row++;

    // More sundry non-stack registers

    if (m_pTargetModel->GetCpuLevel() >= TargetModel::CpuLevel::kCpuLevel68010)
    {
        AddReg32(1, row, Registers::DFC, m_prevRegs, m_currRegs); AddReg32(16, row, Registers::SFC, m_prevRegs, m_currRegs);
        row++;
    }
    if (m_pTargetModel->GetCpuLevel() >= TargetModel::CpuLevel::kCpuLevel68020)
    {
        // 68020
        AddReg32(0, row, Registers::CAAR, m_prevRegs, m_currRegs);
        AddReg16(15, row, Registers::CACR, m_prevRegs, m_currRegs);
        const int x = 28;
        AddCACRBit(x+0, row, m_prevRegs, m_currRegs, Registers::CACRBits::WA, "WA");
        AddCACRBit(x+7, row, m_prevRegs, m_currRegs, Registers::CACRBits::DBE, "DBE");
        AddCACRBit(x+13, row, m_prevRegs, m_currRegs, Registers::CACRBits::CD, "CD");
        AddCACRBit(x+18, row, m_prevRegs, m_currRegs, Registers::CACRBits::FD, "FD");
        AddCACRBit(x+23, row, m_prevRegs, m_currRegs, Registers::CACRBits::ED, "ED");

        AddCACRBit(x+30, row, m_prevRegs, m_currRegs, Registers::CACRBits::IBE, "IBE");
        AddCACRBit(x+36, row, m_prevRegs, m_currRegs, Registers::CACRBits::CI, "CI");
        AddCACRBit(x+41, row, m_prevRegs, m_currRegs, Registers::CACRBits::FI, "FI");
        AddCACRBit(x+46, row, m_prevRegs, m_currRegs, Registers::CACRBits::EI, "EI");
        row++;
        row++;
    }

    // Sundry info
    AddToken(0, row, QString::asprintf("VBL: %10u Frame Cycles: %6u", GET_REG(m_currRegs, VBL), GET_REG(m_currRegs, FrameCycles)), TokenType::kNone);
    row++;
    AddToken(0, row, QString::asprintf("HBL: %10u Line Cycles:  %6u", GET_REG(m_currRegs, HBL), GET_REG(m_currRegs, LineCycles)), TokenType::kNone);
    row++;

    // Tokens have moved, so check again
    UpdateTokenUnderMouse();
    update();
}

void RegisterWidget::UpdateFont()
{
    m_monoFont = m_pSession->GetSettings().m_font;
    QFontMetrics info(m_monoFont);
    m_yAscent = info.ascent();
    m_lineHeight = info.lineSpacing();
    m_charWidth = info.horizontalAdvance("0");
}

QString RegisterWidget::FindSymbol(uint32_t addr)
{
    return DescribeSymbol(m_pTargetModel->GetSymbolTable(), addr & 0xffffff);
}

int RegisterWidget::AddToken(int x, int y, QString text, TokenType type, uint32_t subIndex, TokenColour colour)
{
    Token tok;
    tok.x = x;
    tok.y = y;
    tok.text = text;
    tok.type = type;
    tok.subIndex = subIndex;
    tok.colour = colour;
    m_tokens.push_back(tok);
    // Return X position
    return tok.x + text.size();
}

int RegisterWidget::AddReg16(int x, int y, uint32_t regIndex, const Registers& prevRegs, const Registers& regs)
{
    TokenColour highlight = (regs.m_value[regIndex] != prevRegs.m_value[regIndex]) ? kChanged : kNormal;

    QString label = QString::asprintf("%s:",  Registers::s_names[regIndex]);
    QString value = QString::asprintf("%04x", regs.m_value[regIndex]);
    AddToken(x, y, label, TokenType::kRegister, regIndex, TokenColour::kNormal);
    return AddToken(x + label.size() + 1, y, value, TokenType::kRegister, regIndex, highlight);
}

int RegisterWidget::AddReg32(int x, int y, uint32_t regIndex, const Registers& prevRegs, const Registers& regs)
{
    TokenColour highlight = (regs.m_value[regIndex] != prevRegs.m_value[regIndex]) ? kChanged : kNormal;

    QString label = QString::asprintf("%s:",  Registers::s_names[regIndex]);
    QString value = QString::asprintf("%08x", regs.m_value[regIndex]);
    AddToken(x, y, label, TokenType::kRegister, regIndex, TokenColour::kNormal);
    return AddToken(x + label.size() + 1, y, value, TokenType::kRegister, regIndex, highlight);
}

int RegisterWidget::AddSRBit(int x, int y, const Registers& prevRegs, const Registers& regs, uint32_t bit, const char* pName)
{
    uint32_t valNew = (regs.m_value[Registers::SR] >> bit) & 1;
    uint32_t valOld = (prevRegs.m_value[Registers::SR] >> bit) & 1;

    TokenColour highlight = (valNew != valOld) ? kChanged : kNormal;
    QString text = QString::asprintf("%s=%x", pName, valNew);
    return AddToken(x, y, QString(text), TokenType::kStatusRegisterBit, bit, highlight);
}

int RegisterWidget::AddCACRBit(int x, int y, const Registers& prevRegs, const Registers& regs, uint32_t bit, const char* pName)
{
    uint32_t valNew = (regs.m_value[Registers::CACR] >> bit) & 1;
    uint32_t valOld = (prevRegs.m_value[Registers::CACR] >> bit) & 1;

    TokenColour highlight = (valNew != valOld) ? kChanged : kNormal;
    QString text = QString::asprintf("%s=%x", pName, valNew);
    return AddToken(x, y, QString(text), TokenType::kCACRBit, bit, highlight);
}

int RegisterWidget::AddSymbol(int x, int y, uint32_t address)
{
    QString symText = FindSymbol(address & 0xffffff);
    if (!symText.size())
        return x;

    QString comment = DescribeSymbolComment(m_pTargetModel->GetSymbolTable(), address);
    if (!comment.isEmpty())
        symText += " ;" + comment;
    return AddToken(x, y, symText, TokenType::kSymbol, address);
}

QString RegisterWidget::GetTooltipText(const RegisterWidget::Token& token)
{
    switch (token.type)
    {
    case TokenType::kRegister:
        {
            uint32_t value = m_currRegs.Get(token.subIndex);
            uint32_t prevValue = m_prevRegs.Get(token.subIndex);
            return CreateNumberTooltip(value, prevValue);
        }
    case TokenType::kSymbol:
        return QString::asprintf("Original address: $%08x", token.subIndex);
    case TokenType::kStatusRegisterBit:
         return CreateSRTooltip(m_currRegs.Get(Registers::SR), token.subIndex);
    case TokenType::kCACRBit:
         return CreateCACRTooltip(m_currRegs.Get(Registers::CACR), token.subIndex);
    default:
        break;
    }
    return "";
}

void RegisterWidget::UpdateTokenUnderMouse()
{
    // Update the token
    m_tokenUnderMouseIndex = -1;
    for (int i = 0; i < m_tokens.size(); ++i)
    {
        const Token& tok = m_tokens[i];
        if (tok.rect.contains(m_mousePos))
        {
            m_tokenUnderMouseIndex = i;
            m_tokenUnderMouse = tok;
            break;
        }
    }
}

int RegisterWidget::GetPixelFromRow(int row) const
{
    return Session::kWidgetBorderY + row * m_lineHeight;
}

int RegisterWidget::GetRowFromPixel(int y) const
{
    if (!m_lineHeight)
        return 0;
    return (y - Session::kWidgetBorderY) / m_lineHeight;
}

