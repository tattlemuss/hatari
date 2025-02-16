#include "registerwidget.h"

#include <QPainter>
#include <QPen>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QSettings>
#include <QToolTip>

#include "hopper/buffer.h"
#include "hopper56/buffer.h"

#include "../models/session.h"
#include "../models/stringformat.h"
#include "../hardware/tos.h"
#include "symboltext.h"
#include "hopper56/buffer.h"

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
        ref << ((val >= 32 && val < 128) ? QString(val) : ".");
    }
    ref << "\"\n";

    return final;
}

static QString CreateDspNumberTooltip(uint32_t value, uint32_t prevValue)
{
    value &= 0xffffff;
    prevValue &= 0xffffff;
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

    const uint32_t topBit = 0x800000U;
    int32_t signedVal = static_cast<int32_t>(value);
    if (value & topBit)
        signedVal |= 0xff000000;

    double frac = 1.0 * signedVal / topBit;
    ref << QString::asprintf("Fractional: %f\n", frac);

    if (value & topBit)
        ref << QString::asprintf("Long: %u (%d)\n", value, signedVal);
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
        ref << ((val >= 32 && val < 128) ? QString(val) : ".");
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

static QString CreateDspSRTooltip(uint32_t srRegValue, uint32_t registerBit)
{
    uint32_t valSet = (srRegValue >> registerBit) & 1;
    return QString::asprintf("%s = %s", DspRegisters::GetSRBitName(registerBit),
                             valSet ? "TRUE" : "False");
}

static QString CreateDspOMRTooltip(uint32_t srRegValue, uint32_t registerBit)
{
    uint32_t valSet = (srRegValue >> registerBit) & 1;
    return QString::asprintf("%s = %s", DspRegisters::GetOMRBitDesc(registerBit),
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
    // The widget is now of fixed size (determined by layout).
    // This is because the scrollarea containing it controls the
    // visible UI araa.
    // TODO: I'll need to make sure the scrollarea knows about
    // resizing, if the user changes the chosen register items.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_showCpu = true;
    m_showDsp = true;
    m_pShowCpuAction = new QAction("Show CPU", this);
    m_pShowCpuAction->setCheckable(true);
    m_pShowDspAction = new QAction("Show DSP", this);
    m_pShowDspAction->setCheckable(true);
    m_pFilterMenu = new QMenu("Filters", this);
    m_pFilterMenu->addAction(m_pShowCpuAction);
    m_pFilterMenu->addAction(m_pShowDspAction);

    // Listen for target changes
    connect(m_pTargetModel, &TargetModel::startStopChangedSignal,        this, &RegisterWidget::startStopChanged);
    connect(m_pTargetModel, &TargetModel::registersChangedSignal,        this, &RegisterWidget::registersChanged);
    connect(m_pTargetModel, &TargetModel::connectChangedSignal,          this, &RegisterWidget::connectChanged);
    connect(m_pTargetModel, &TargetModel::memoryChangedSignal,           this, &RegisterWidget::memoryChanged);
    connect(m_pTargetModel, &TargetModel::symbolTableChangedSignal,      this, &RegisterWidget::symbolTableChanged);
    connect(m_pTargetModel, &TargetModel::startStopChangedSignalDelayed, this, &RegisterWidget::startStopDelayed);
    connect(m_pTargetModel, &TargetModel::mainStateCompletedSignal,      this, &RegisterWidget::mainStateUpdated);

    // Context menus
    connect(m_pShowCpuAction,   &QAction::triggered,                     this, &RegisterWidget::showCpuTriggered);
    connect(m_pShowDspAction,   &QAction::triggered,                     this, &RegisterWidget::showDspTriggered);

    connect(m_pSession, &Session::settingsChanged,  this, &RegisterWidget::settingsChanged);

    setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    setMouseTracking(true);

    // This updates checkboxes
    loadSettings();
    UpdateFont();
    // Handle non-connection at start
    PopulateRegisters();
}

RegisterWidget::~RegisterWidget()
{
}

void RegisterWidget::loadSettings()
{
    QSettings settings;
    settings.beginGroup("RegisterWidget");

    m_showCpu = settings.value("showCpu", QVariant(true)).toBool();
    m_showDsp = settings.value("showDsp", QVariant(true)).toBool();
    m_pShowCpuAction->setChecked(m_showCpu);
    m_pShowDspAction->setChecked(m_showDsp);
    settings.endGroup();
}

void RegisterWidget::saveSettings()
{
    QSettings settings;
    settings.beginGroup("RegisterWidget");

    settings.setValue("showCpu", m_showCpu);
    settings.setValue("showDsp", m_showDsp);
    settings.endGroup();
}

void RegisterWidget::paintEvent(QPaintEvent * ev)
{
    QWidget::paintEvent(ev);

    QPainter painter(this);
    painter.setFont(m_monoFont);
    QFontMetrics info(painter.fontMetrics());
    const QPalette& pal = this->palette();

    painter.setClipRect(this->rect());
    //().boundingRect());

    const QBrush& br = pal.window().color();
    painter.fillRect(this->rect(), br);
    painter.setPen(QPen(pal.dark(), hasFocus() ? 6 : 2));
    painter.drawRect(this->rect());

    // Draw Rulers
    int rectW = this->rect().right() - m_charWidth * 2;
    painter.setPen(QPen(pal.dark(), 2));
    for (int i = 0; i < m_rulers.size(); ++i)
    {
        int y = GetPixelFromRow(m_rulers[i].y);
        int h = m_lineHeight;
        painter.drawLine(0, y, this->rect().width(), y);
        painter.drawText(0, y, rectW, h,
                         Qt::AlignRight, m_rulers[i].text);
    }

    // Draw Tokens
    painter.setPen(QPen(pal.dark(), 1));
    for (int i = 0; i < m_tokens.size(); ++i)
    {
        Token& tok = m_tokens[i];

        int x = GetPixelFromCol(tok.x);
        int y = GetPixelFromRow(tok.y);
        int w = info.horizontalAdvance(tok.text);
        int h = m_lineHeight;
        tok.rect.setRect(x, y, w, h);

        QColor col = pal.dark().color();

        if (tok.colour == TokenColour::kNormal)
            col = pal.text().color();
        else if (tok.colour == TokenColour::kChanged)
            col = Qt::red;
        else if (tok.colour == TokenColour::kInactive)
            col = pal.light().color();
        else if (tok.colour == TokenColour::kCode)
            col = Qt::darkGreen;

        if (i == m_tokenUnderMouseIndex && tok.type != TokenType::kNone)
        {
            painter.setBrush(pal.highlight());
            painter.setPen(Qt::NoPen);
            painter.drawRect(tok.rect);
            painter.setPen(pal.highlightedText().color());
        }
        else if (tok.invert)
        {
            painter.setBrush(col);
            painter.setPen(Qt::NoPen);
            painter.drawRect(tok.rect);
            painter.setPen(pal.highlightedText().color());
        }
        else
        {
            painter.setPen(col);
        }

        painter.drawText(x, m_yAscent + y, tok.text);
    }
}

void RegisterWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_mousePos = event->localPos();
    UpdateTokenUnderMouse();
    QWidget::mouseMoveEvent(event);
    update();
}

void RegisterWidget::contextMenuEvent(QContextMenuEvent *event)
{
    // Right click menus are instantiated on demand, so we can
    // dynamically add to them
    QMenu menu(this);

    menu.addMenu(m_pFilterMenu);

    // Add the default actions
    MemSpace spaceUnderMouse = MEM_CPU;
    if (m_tokenUnderMouse.type == TokenType::kRegister)
    {
        m_addressUnderMouse = m_currRegs.Get(m_tokenUnderMouse.subIndex);
        spaceUnderMouse = MEM_CPU;
        m_showAddressMenu.Set("CPU Reg", m_pSession, spaceUnderMouse, m_addressUnderMouse);
        m_showAddressMenu.AddTo(&menu);
    }
    else if (m_tokenUnderMouse.type == TokenType::kSymbol)
    {
        m_addressUnderMouse = m_tokenUnderMouse.subIndex;
        spaceUnderMouse = MEM_CPU;
        m_showAddressMenu.Set("Address", m_pSession, spaceUnderMouse, m_addressUnderMouse);
        m_showAddressMenu.AddTo(&menu);
    }
    else if (m_tokenUnderMouse.type == TokenType::kDspRegister)
    {
        m_addressUnderMouse = m_currDspRegs.Get(m_tokenUnderMouse.subIndex);
        m_showAddressMenuDsp.Set("DSP Reg", m_pSession, m_addressUnderMouse);
        m_showAddressMenuDsp.AddTo(&menu);
    }

    // Run it
    menu.exec(event->globalPos());
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
        m_mousePos = pEnterEvent->localPos();
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
        m_prevDspRegs = m_pTargetModel->GetDspRegs();

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
        m_rulers.clear();
        m_tokenUnderMouseIndex = -1;
        AddToken(1, 1, tr("Running, Ctrl+R to break..."), TokenType::kNone, 0);
        update();
    }
}

void RegisterWidget::mainStateUpdated()
{
    // Disassemble the first instruction
    m_disasm.lines.clear();
    const Memory* pMem;
    pMem = m_pTargetModel->GetMemory(MemorySlot::kMainPC);
    if (pMem)
    {
        hop68::buffer_reader disasmBuf(pMem->GetData(), pMem->GetSize(), pMem->GetAddress());
        Disassembler::decode_buf(disasmBuf, m_disasm, m_pTargetModel->GetDisasmSettings(), pMem->GetAddress(), 2);
    }

    m_disasmDsp.lines.clear();
    pMem = m_pTargetModel->GetMemory(MemorySlot::kMainDspPC);
    if (pMem)
    {
        hop56::buffer_reader disasmBuf(pMem->GetData(), pMem->GetSize(), pMem->GetAddress());
        hop56::decode_settings dummy;
        Disassembler56::decode_buf(disasmBuf, m_disasmDsp, dummy, pMem->GetAddress(), 1);
    }

    PopulateRegisters();
    update();
}

void RegisterWidget::showCpuTriggered()
{
    m_showCpu = m_pShowCpuAction->isChecked();
    PopulateRegisters();
}

void RegisterWidget::showDspTriggered()
{
    m_showDsp = m_pShowDspAction->isChecked();
    PopulateRegisters();
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
    if (m_pTargetModel->IsMainStateUpdating())
        return;
    PopulateRegisters();
}

void RegisterWidget::PopulateRegisters()
{
    m_tokens.clear();
    m_rulers.clear();
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
    m_currDspRegs = m_pTargetModel->GetDspRegs();

    // Row 0 -- PC, and symbol if applicable
    int row = 0;
    if (m_showCpu)
    {
        AddRuler(row, "CPU");
        AddReg32(2, row, Registers::PC);
        QString sym = FindSymbol(GET_REG(m_currRegs, PC) & 0xffffff);
        if (sym.size() != 0)
            AddToken(16, row, MakeBracket(sym), TokenType::kSymbol, GET_REG(m_currRegs, PC));

        // Status register
        ++row;
        AddReg16(2, row, Registers::SR);
        int col = 11;
        col = 1 + AddSRBit(col, row, Registers::SRBits::kTrace1, "T");
        col = 1 + AddSRBit(col, row, Registers::SRBits::kSupervisor, "S");
        col = AddSRBit(col, row, Registers::SRBits::kX, "X");
        col = AddSRBit(col, row, Registers::SRBits::kN, "N");
        col = AddSRBit(col, row, Registers::SRBits::kZ, "Z");
        col = AddSRBit(col, row, Registers::SRBits::kV, "V");
        col = AddSRBit(col, row, Registers::SRBits::kC, "C");
        QString iplLevel = QString::asprintf("IPL=%u", (m_currRegs.m_value[Registers::SR] >> 8 & 0x7));
        col = AddToken(col + 2, row, iplLevel, TokenType::kNone);

        row += 2;

        // Row 1 -- instruction and analysis
        if (m_disasm.lines.size() > 0)
        {
            QString disasmText = ">> ";
            QTextStream ref(&disasmText);

            const hop68::instruction& inst = m_disasm.lines[0].inst;
            Disassembler::print_terse(inst, m_disasm.lines[0].address, ref,m_pSession->GetSettings().m_bDisassHexNumerics);

            bool branchTaken;
            if (DisAnalyse::isBranch(inst, m_currRegs, branchTaken))
            {
                if (branchTaken)
                    ref << " [TAKEN]";
                else
                    ref << " [NOT TAKEN]";
            }

            col = AddToken(2, row, disasmText, TokenType::kNone, 0, TokenColour::kCode) + 5;

            // Comments
            if (m_disasm.lines.size() != 0)
            {
                int i = 0;
                const hop68::instruction& inst = m_disasm.lines[i].inst;
                bool prevValid = false;
                for (int opIndex = 0; opIndex < 2; ++opIndex)
                {
                    const hop68::operand& op = opIndex == 0 ? inst.op0 : inst.op1;
                    if (op.type != hop68::OpType::INVALID)
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
                        case hop68::OpType::D_DIRECT:
                            eaRef << QString::asprintf("D%d=$%x", op.d_register.reg, regs.GetDReg(op.d_register.reg));
                            col = AddToken(col, row, eaText, TokenType::kRegister, Registers::D0 + op.d_register.reg, TokenColour::kCode) + 1;
                            prevValid = true;
                            break;
                        case hop68::OpType::A_DIRECT:
                            eaRef << QString::asprintf("A%d=$%x", op.a_register.reg, regs.GetAReg(op.a_register.reg));
                            col = AddToken(col, row, eaText, TokenType::kRegister, Registers::A0 + op.a_register.reg, TokenColour::kCode) + 1;
                            prevValid = true;
                            break;
                        case hop68::OpType::SR:
                            eaRef << QString::asprintf("SR=$%x", regs.Get(Registers::SR));
                            col = AddToken(col, row, eaText, TokenType::kRegister, Registers::SR, TokenColour::kCode) + 1;
                            prevValid = true;
                            break;
                        case hop68::OpType::USP:
                            eaRef << QString::asprintf("USP=$%x", regs.Get(Registers::USP));
                            col = AddToken(col, row, eaText, TokenType::kRegister, Registers::USP, TokenColour::kCode) + 1;
                            prevValid = true;
                            break;
                        case hop68::OpType::CCR:
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
        uint32_t ex = GET_REG(m_currRegs, EX);
        if (ex != 0)
            AddToken(4, row, QString::asprintf("EXCEPTION: %s", ExceptionMask::GetName(ex)), TokenType::kNone, 0, TokenColour::kChanged);

        // D-regs // A-regs
        row++;
        AddRuler(row, "CPU Regs");
        for (uint32_t reg = 0; reg < 8; ++reg)
        {
            AddReg32(2, row, Registers::D0 + reg);

            AddReg32(17, row, Registers::A0 + reg); AddSymbol(30, row, m_currRegs.m_value[Registers::A0 + reg]);
            row++;
        }
        AddReg32(16, row, Registers::ISP); AddSymbol(30, row, m_currRegs.m_value[Registers::ISP]);
        row++;
        AddReg32(16, row, Registers::USP); AddSymbol(30, row, m_currRegs.m_value[Registers::USP]);
        row++;

        if (m_pTargetModel->GetCpuLevel() >= TargetModel::CpuLevel::kCpuLevel68020)
        {
            AddReg32(16, row, Registers::MSP); AddSymbol(30, row, m_currRegs.m_value[Registers::MSP]);
            row++;
        }

    }
    // DSP registers
    if (m_showDsp && m_pTargetModel->GetMachineType() == MACHINE_FALCON)
    {
        AddRuler(row, "DSP");
        AddDspReg16(2,  row, DspRegisters::PC);
        ++row;
        int col;
        col = 1 + AddDspReg16(2, row, DspRegisters::SR);
        col = 1 + AddDspSRBit(col, row, DspRegisters::SRBits::kLF, "LF");
        col = 1 + AddDspSRBit(col, row, DspRegisters::SRBits::kDM, "DM");
        col = 1 + AddDspSRBit(col, row, DspRegisters::SRBits::kT, "T");
        col = 1 + AddDspSRBit(col, row, DspRegisters::SRBits::kS1, "S1");
        col = 1 + AddDspSRBit(col, row, DspRegisters::SRBits::kS0, "S0");
        col = 1 + AddDspSRBit(col, row, DspRegisters::SRBits::kI1, "I1");
        col = 1 + AddDspSRBit(col, row, DspRegisters::SRBits::kI0, "I0");
        col = 0 + AddDspSRBit(col, row, DspRegisters::SRBits::kS, "S");
        col = 0 + AddDspSRBit(col, row, DspRegisters::SRBits::kL, "L");
        col = 0 + AddDspSRBit(col, row, DspRegisters::SRBits::kE, "E");
        col = 0 + AddDspSRBit(col, row, DspRegisters::SRBits::kU, "U");
        col = 0 + AddDspSRBit(col, row, DspRegisters::SRBits::kN, "N");
        col = 0 + AddDspSRBit(col, row, DspRegisters::SRBits::kZ, "Z");
        col = 0 + AddDspSRBit(col, row, DspRegisters::SRBits::kV, "V");
        col = 0 + AddDspSRBit(col, row, DspRegisters::SRBits::kC, "C");

        // Row 1 -- instruction and analysis
        if (m_disasmDsp.lines.size() > 0)
        {
            row += 2;
            QString disasmText = ">> ";
            QTextStream ref(&disasmText);

            const hop56::instruction& inst = m_disasmDsp.lines[0].inst;
            Disassembler56::print_terse(inst, ref);
            AddToken(2, row, disasmText, TokenType::kNone, 0, TokenColour::kCode);
            ++row;
        }
        ++row;
        AddRuler(row, "DSP ALU");
        AddDspReg8(  2, row, DspRegisters::A2);
        AddDspReg24(10, row, DspRegisters::A1);
        AddDspReg24(22, row, DspRegisters::A0);
        ++row;
        AddDspReg8(  2, row, DspRegisters::B2);
        AddDspReg24(10, row, DspRegisters::B1);
        AddDspReg24(22, row, DspRegisters::B0);
        ++row;
        AddDspReg24(10,  row, DspRegisters::X1);
        AddDspReg24(22, row, DspRegisters::X0);
        ++row;
        AddDspReg24(10,  row, DspRegisters::Y1);
        AddDspReg24(22, row, DspRegisters::Y0);
        ++row;
        AddRuler(row, "DSP Address");
        for (uint32_t reg = 0; reg < 8; ++reg)
        {
            AddDspReg16( 2, row, DspRegisters::R0 + reg);
            AddDspReg16(13, row, DspRegisters::N0 + reg);
            AddDspReg16(24, row, DspRegisters::M0 + reg);
            row++;
        }
        AddRuler(row, "DSP (other)");
        AddDspReg16(2, row, DspRegisters::LA);
        AddDspReg16(13, row, DspRegisters::LC);
        col = AddDspReg16(23, row, DspRegisters::OMR);
        col = 1 + AddDspOMRBit(col + 1, row, DspRegisters::kDE, "DE");
        col = 1 + AddDspOMRBit(col + 1, row, DspRegisters::kYD, "YD");
        col = 1 + AddDspOMRBit(col + 1, row, DspRegisters::kSD, "SD");

        ++row;
        AddDspReg16(2, row, DspRegisters::SP);
        AddDspReg16(12, row, DspRegisters::SSH);
        AddDspReg16(23, row, DspRegisters::SSL);
        ++row;
    }

    if (m_showCpu)
    {
        // More sundry non-stack registers
        if (m_pTargetModel->GetCpuLevel() >= TargetModel::CpuLevel::kCpuLevel68010)
        {
            AddRuler(row, "CPU: 68010");
            AddReg32(1, row, Registers::DFC); AddReg32(16, row, Registers::SFC);
            row++;
        }
        if (m_pTargetModel->GetCpuLevel() >= TargetModel::CpuLevel::kCpuLevel68020)
        {
            AddRuler(row, "CPU: 68020");
            // 68020
            AddReg32(0, row, Registers::CAAR);
            AddReg16(15, row, Registers::CACR);
            int x = 28;
            x = 1 + AddCACRBit(x, row, Registers::CACRBits::WA, "WA");
            x = 1 + AddCACRBit(x, row, Registers::CACRBits::DBE, "DBE");
            x = 1 + AddCACRBit(x, row, Registers::CACRBits::CD, "CD");
            x = 1 + AddCACRBit(x, row, Registers::CACRBits::FD, "FD");
            x = 3 + AddCACRBit(x, row, Registers::CACRBits::ED, "ED");

            x = 1 + AddCACRBit(x, row, Registers::CACRBits::IBE, "IBE");
            x = 1 + AddCACRBit(x, row, Registers::CACRBits::CI, "CI");
            x = 1 + AddCACRBit(x, row, Registers::CACRBits::FI, "FI");
            x = 1 + AddCACRBit(x, row, Registers::CACRBits::EI, "EI");
            row++;
            row++;
        }
    }

    AddRuler(row, "Hatari");
    // Variables
    // Sundry info
    AddToken(0, row, QString::asprintf("VBL: %10u Frame Cycles: %6u", GET_REG(m_currRegs, VBL), GET_REG(m_currRegs, FrameCycles)), TokenType::kNone);
    row++;
    AddToken(0, row, QString::asprintf("HBL: %10u Line Cycles:  %6u", GET_REG(m_currRegs, HBL), GET_REG(m_currRegs, LineCycles)), TokenType::kNone);
    row++;

    // Tokens have moved, so check again
    UpdateTokenUnderMouse();
    this->resize(GetPixelFromCol(80), GetPixelFromRow(row));
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

int RegisterWidget::AddToken(int x, int y, QString text, TokenType type, uint32_t subIndex, TokenColour colour, bool invert)
{
    Token tok;
    tok.x = x;
    tok.y = y;
    tok.text = text;
    tok.type = type;
    tok.subIndex = subIndex;
    tok.colour = colour;
    tok.invert = invert;
    m_tokens.push_back(tok);
    // Return X position
    return tok.x + text.size();
}

int RegisterWidget::AddReg16(int x, int y, uint32_t regIndex)
{
    const Registers& prevRegs(m_prevRegs); const Registers& regs(m_currRegs);
    TokenColour highlight = (regs.m_value[regIndex] != prevRegs.m_value[regIndex]) ? kChanged : kNormal;

    QString label = QString::asprintf("%s:",  Registers::s_names[regIndex]);
    QString value = QString::asprintf("%04x", regs.m_value[regIndex]);
    AddToken(x, y, label, TokenType::kRegister, regIndex, TokenColour::kNormal);
    return AddToken(x + label.size() + 1, y, value, TokenType::kRegister, regIndex, highlight);
}

int RegisterWidget::AddReg32(int x, int y, uint32_t regIndex)
{
    const Registers& prevRegs(m_prevRegs); const Registers& regs(m_currRegs);
    TokenColour highlight = (regs.m_value[regIndex] != prevRegs.m_value[regIndex]) ? kChanged : kNormal;

    QString label = QString::asprintf("%s:",  Registers::s_names[regIndex]);
    QString value = QString::asprintf("%08x", regs.m_value[regIndex]);
    AddToken(x, y, label, TokenType::kRegister, regIndex, TokenColour::kNormal);
    return AddToken(x + label.size() + 1, y, value, TokenType::kRegister, regIndex, highlight);
}

int RegisterWidget::AddDspReg24(int x, int y, uint32_t regIndex)
{
    const DspRegisters& prevRegs(m_prevDspRegs); const DspRegisters& regs(m_currDspRegs);
    TokenColour highlight = (regs.Get(regIndex) != prevRegs.Get(regIndex)) ? kChanged : kNormal;

    QString label = QString::asprintf("%s:",  DspRegisters::s_names[regIndex]);
    QString value = QString::asprintf("%06lx", regs.Get(regIndex));
    AddToken(x, y, label, TokenType::kDspRegister, regIndex, TokenColour::kNormal);
    return AddToken(x + label.size() + 1, y, value, TokenType::kDspRegister, regIndex, highlight);
}

int RegisterWidget::AddDspReg16(int x, int y, uint32_t regIndex)
{
    const DspRegisters& prevRegs(m_prevDspRegs); const DspRegisters& regs(m_currDspRegs);
    TokenColour highlight = (regs.Get(regIndex) != prevRegs.Get(regIndex)) ? kChanged : kNormal;

    QString label = QString::asprintf("%s:",  DspRegisters::s_names[regIndex]);
    QString value = QString::asprintf("%04lx", regs.Get(regIndex));
    AddToken(x, y, label, TokenType::kDspRegister, regIndex, TokenColour::kNormal);
    return AddToken(x + label.size() + 1, y, value, TokenType::kDspRegister, regIndex, highlight);
}

int RegisterWidget::AddDspReg8(int x, int y, uint32_t regIndex)
{
    const DspRegisters& prevRegs(m_prevDspRegs); const DspRegisters& regs(m_currDspRegs);
    TokenColour highlight = (regs.Get(regIndex) != prevRegs.Get(regIndex)) ? kChanged : kNormal;

    QString label = QString::asprintf("%s:",  DspRegisters::s_names[regIndex]);
    QString value = QString::asprintf("%02lx", regs.Get(regIndex));
    AddToken(x, y, label, TokenType::kDspRegister, regIndex, TokenColour::kNormal);
    return AddToken(x + label.size() + 1, y, value, TokenType::kDspRegister, regIndex, highlight);
}

int RegisterWidget::AddSRBit(int x, int y, uint32_t bit, const char* pName)
{
    const Registers& prevRegs(m_prevRegs); const Registers& regs(m_currRegs);
    uint32_t valNew = (regs.m_value[Registers::SR] >> bit) & 1;
    uint32_t valOld = (prevRegs.m_value[Registers::SR] >> bit) & 1;

    TokenColour highlight = (valNew != valOld) ? kChanged : kNormal;
    QString text = pName;
    return AddToken(x, y, text, TokenType::kStatusRegisterBit, bit, highlight, valNew != 0);
}

int RegisterWidget::AddDspSRBit(int x, int y, uint32_t bit, const char* pName)
{
    const DspRegisters& prevRegs(m_prevDspRegs); const DspRegisters& regs(m_currDspRegs);
    uint32_t valNew = (regs.Get(DspRegisters::SR) >> bit) & 1;
    uint32_t valOld = (prevRegs.Get(DspRegisters::SR) >> bit) & 1;

    TokenColour highlight = (valNew != valOld) ? kChanged : kNormal;
    QString text = pName;
    return AddToken(x, y, QString(text), TokenType::kDspStatusRegisterBit, bit, highlight, valNew != 0);
}

int RegisterWidget::AddDspOMRBit(int x, int y, uint32_t bit, const char* pName)
{
    const DspRegisters& prevRegs(m_prevDspRegs); const DspRegisters& regs(m_currDspRegs);
    uint32_t valNew = (regs.Get(DspRegisters::OMR) >> bit) & 1;
    uint32_t valOld = (prevRegs.Get(DspRegisters::OMR) >> bit) & 1;

    TokenColour highlight = (valNew != valOld) ? kChanged : kNormal;
    QString text = pName;
    return AddToken(x, y, QString(text), TokenType::kDspOMRBit, bit, highlight, valNew != 0);
}

int RegisterWidget::AddCACRBit(int x, int y, uint32_t bit, const char* pName)
{
    const Registers& prevRegs(m_prevRegs); const Registers& regs(m_currRegs);
    uint32_t valNew = (regs.m_value[Registers::CACR] >> bit) & 1;
    uint32_t valOld = (prevRegs.m_value[Registers::CACR] >> bit) & 1;

    TokenColour highlight = (valNew != valOld) ? kChanged : kNormal;
    QString text = pName;
    return AddToken(x, y, QString(text), TokenType::kCACRBit, bit, highlight, valNew != 0);
}

int RegisterWidget::AddSymbol(int x, int y, uint32_t address)
{
    QString symText = FindSymbol(address & 0xffffff);
    if (!symText.size())
        return x;

    QString comment = DescribeSymbolComment(m_pTargetModel->GetSymbolTable(), address);
    if (!comment.isEmpty())
        symText += " ;" + comment;
    return AddToken(x, y, symText, TokenType::kSymbol, address, TokenColour::kCode);
}

void RegisterWidget::AddRuler(int y, QString text)
{
    Ruler r;
    r.y = y; r.text = text;
    m_rulers.push_back(r);
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
    case TokenType::kDspRegister:
        {
            uint32_t value = m_currDspRegs.Get(token.subIndex);
            uint32_t prevValue = m_prevDspRegs.Get(token.subIndex);
            return CreateDspNumberTooltip(value, prevValue);
        }
    case TokenType::kSymbol:
        return QString::asprintf("Original address: $%08x", token.subIndex);
    case TokenType::kStatusRegisterBit:
         return CreateSRTooltip(m_currRegs.Get(Registers::SR), token.subIndex);
    case TokenType::kDspStatusRegisterBit:
         return CreateDspSRTooltip(m_currDspRegs.Get(DspRegisters::SR), token.subIndex);
    case TokenType::kDspOMRBit:
         return CreateDspOMRTooltip(m_currDspRegs.Get(DspRegisters::OMR), token.subIndex);
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
    m_tokenUnderMouse.type = TokenType::kNone;
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

int RegisterWidget::GetPixelFromCol(int col) const
{
    return Session::kWidgetBorderX + col * m_charWidth;
}


int RegisterWidget::GetRowFromPixel(int y) const
{
    if (!m_lineHeight)
        return 0;
    return (y - Session::kWidgetBorderY) / m_lineHeight;
}
