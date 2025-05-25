#include "hardwarewindow.h"

#include <QClipboard>
#include <QContextMenuEvent>
#include <QDebug>
#include <QGuiApplication>
#include <QPushButton>
#include <QSettings>
#include <QTreeView>
#include <QVBoxLayout>
#include <QToolTip>

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../models/session.h"
#include "../models/stringformat.h"
#include "../hardware/hardware_st.h"
#include "../hardware/regs_st.h"
#include "nonantialiasimage.h"
#include "quicklayout.h"
#include "showaddressactions.h"
#include "symboltext.h"

//-----------------------------------------------------------------------------
// Wrappers to get memory from multiple memory slots
static bool HasAddressMulti(const TargetModel* pModel, uint32_t address, uint32_t numBytes)
{
    for (int i = MemorySlot::kHardwareWindowStart; i <= MemorySlot::kHardwareWindowEnd; ++i)
    {
        const Memory* pMem = pModel->GetMemory(static_cast<MemorySlot>(i));
        if (!pMem)
            continue;
        if (pMem->HasCpuRange(address, numBytes))
            return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
// Search multiple memory slots for the given target memory address
uint32_t ReadAddressMulti(const TargetModel* pModel, uint32_t address, uint32_t numBytes)
{
    for (int i = MemorySlot::kHardwareWindowStart; i <= MemorySlot::kHardwareWindowEnd; ++i)
    {
        const Memory* pMem = pModel->GetMemory(static_cast<MemorySlot>(i));
        if (!pMem)
            continue;
        uint32_t val;
        if (!pMem->ReadCpuMulti(address, numBytes, val))
            continue;
        return val;
    }
    return 0;
}

//-----------------------------------------------------------------------------
bool GetField(const TargetModel* pModel, const stgen::FieldDef& def, QString& result)
{
    if (!HasAddressMulti(pModel, def.regAddr, def.size))
        return false;

    uint32_t regVal = ReadAddressMulti(pModel, def.regAddr, def.size);
    uint32_t extracted = (regVal >> def.shift) & def.mask;
    if (def.strings)
        result = QString::asprintf("%s ($%x)", stgen::GetString(def.strings, extracted), extracted);
    else
        result = QString::asprintf("%u ($%x)", extracted, extracted);
    return true;
}

//-----------------------------------------------------------------------------
bool GetRegSigned16(const TargetModel* pModel, uint32_t addr, QString& result)
{
    if (!HasAddressMulti(pModel, addr, 2))
        return false;

    uint32_t regVal = ReadAddressMulti(pModel, addr, 2);
    int16_t extracted = static_cast<int16_t>(regVal);
    if (extracted < 0)
        result = QString::asprintf("-$%x (%d)", -extracted, extracted);
    else
        result = QString::asprintf("$%x (%d)", extracted, extracted);
    return true;
}

//-----------------------------------------------------------------------------
bool GetRegBinary16(const TargetModel* pModel, uint32_t addr, QString& result)
{
    if (!HasAddressMulti(pModel, addr, 2))
        return false;

    uint32_t regVal = ReadAddressMulti(pModel, addr, 2);
    uint32_t extracted = regVal;
    char binText[17];
    // This is of course terrible
    for (uint32_t i = 0; i < 16; ++i)
    {
        uint32_t bit = 15 - i;
        binText[i] = ((extracted & (1 << bit))) ? '1' : '0';
    }
    // Add terminator
    binText[16] = 0;
    result = QString(binText) + QString::asprintf(" ($%04x)", extracted);
    return true;
}

//-----------------------------------------------------------------------------
bool GetFieldVal(const Memory& mem, const stgen::FieldDef& def, uint32_t& result)
{
    if (!mem.HasCpuRange(def.regAddr, def.size))
        return false;

    uint32_t regVal;
    mem.ReadCpuMulti(def.regAddr, def.size, regVal);
    uint32_t extracted = (regVal >> def.shift) & def.mask;
    result = extracted;
    return true;
}

//-----------------------------------------------------------------------------
class HardwareBase
{
public:
    HardwareBase() :
        m_memAddress(~0U),
        m_changed(false),
        m_pParent(nullptr),
        m_rowIndex(0)
    {}

    virtual ~HardwareBase();
    virtual bool isHeader() const { return false; }
    virtual bool GetBrush(QBrush& /*res*/) { return false; }

    HardwareBase* AddChild(HardwareBase* pField)
    {
        pField->m_pParent = this;
        pField->m_rowIndex = this->m_children.size();

        m_children.push_back(pField);
        return pField;
    }

    QString                    m_title;
    QString                    m_text;
    QString                    m_tooltip;
    uint32_t                   m_memAddress;            // For right-click menu, or ~0U
    bool                       m_changed;

    HardwareBase*              m_pParent;
    QVector<HardwareBase*>     m_children;
    int                        m_rowIndex;
};

//-----------------------------------------------------------------------------
class HardwareField : public HardwareBase
{
public:
    HardwareField()
    {
    }
    virtual ~HardwareField();
    virtual bool isHeader() const { return false; }
    virtual bool Update(const TargetModel*)
    {
        return false;
    }

protected:
};

//-----------------------------------------------------------------------------
class HardwareHeader : public HardwareBase
{
public:
    HardwareHeader(QString title, QString subTitle)
    {
        m_title = title;
        m_text = subTitle;
    }

    virtual ~HardwareHeader();
    virtual bool isHeader() const { return true; }
};

//-----------------------------------------------------------------------------
HardwareField::~HardwareField()
{
}

//-----------------------------------------------------------------------------
class HardwareFieldRegEnum : public HardwareField
{
public:
    HardwareFieldRegEnum(const stgen::FieldDef& def) :
        m_def(def)
    {
        m_memAddress = m_def.regAddr;
        m_tooltip = def.comment;
    }

    bool Update(const TargetModel* pTarget);

    const stgen::FieldDef&   m_def;
};

//-----------------------------------------------------------------------------
// Show 16-bit register as binary value
class HardwareFieldRegBinary16 : public HardwareField
{
public:
    HardwareFieldRegBinary16(uint32_t addr) :
        m_addr(addr)
    {
        m_memAddress = m_addr;
    }

    bool Update(const TargetModel* pTarget);

    const uint32_t m_addr;
};

//-----------------------------------------------------------------------------
// Show 16-bit register as signed value
class HardwareFieldRegSigned16 : public HardwareField
{
public:
    HardwareFieldRegSigned16(uint32_t addr) :
        m_addr(addr)
    {
        m_memAddress = m_addr;
    }

    bool Update(const TargetModel* pTarget);

    const uint32_t m_addr;
};

//-----------------------------------------------------------------------------
class HardwareFieldMultiField : public HardwareField
{
public:
    HardwareFieldMultiField(const stgen::FieldDef** defs) :
        m_pDefs(defs)
    {
        // Simply use the first register address
        if (defs && defs[0])
            m_memAddress = defs[0]->regAddr;
    }

    bool Update(const TargetModel* pTarget);

    const stgen::FieldDef**  m_pDefs;
};

//-----------------------------------------------------------------------------
class HardwareFieldAddr : public HardwareField
{
public:
    enum Type
    {
        ScreenBase,
        ScreenCurr,
        BltSrc,
        BltDst,
        BasePage,
        Mfp,
        DMASndStart,
        DMASndCurr,
        DMASndEnd,
    };
    HardwareFieldAddr(Type type, uint32_t address = 0);
    HardwareFieldAddr(uint32_t address);
    ~HardwareFieldAddr();

    bool Update(const TargetModel* pTarget);
private:
    Type m_type;
    uint32_t m_address;
};

//-----------------------------------------------------------------------------
class HardwareFieldYm : public HardwareField
{
public:
    HardwareFieldYm(int index) :
        m_index(index)
    {
    }

    bool Update(const TargetModel* pTarget);
    int m_index;
};

//-----------------------------------------------------------------------------
class HardwareFieldYmPeriod : public HardwareField
{
public:
    HardwareFieldYmPeriod(int index) :
        m_index(index)
    {
    }

    bool Update(const TargetModel* pTarget);
    int m_index;
};

//-----------------------------------------------------------------------------
class HardwareFieldYmEnvShape : public HardwareField
{
public:
    HardwareFieldYmEnvShape() {}

    bool Update(const TargetModel* pTarget);
};

//-----------------------------------------------------------------------------
class HardwareFieldYmMixer : public HardwareField
{
public:
    HardwareFieldYmMixer() {}

    bool Update(const TargetModel* pTarget);
};

//-----------------------------------------------------------------------------
class HardwareFieldYmVolume : public HardwareField
{
public:
    HardwareFieldYmVolume(int index) :
        m_index(index)
    {
    }

    bool Update(const TargetModel* pTarget);
    int m_index;
};

//-----------------------------------------------------------------------------
class HardwareBitmap : public HardwareField
{
public:
    HardwareBitmap(Session *pSession);
    virtual ~HardwareBitmap();

    NonAntiAliasImage*      m_pImage;
};

//-----------------------------------------------------------------------------
class HardwareBitmapBlitterHalftone : public HardwareBitmap
{
public:
    HardwareBitmapBlitterHalftone(Session* pSession) :
        HardwareBitmap(pSession)
    {
        m_pImage->setFixedSize(64, 64);
    }

    bool Update(const TargetModel* pTarget);
};

//-----------------------------------------------------------------------------
// Show 16-bit register as binary value
class HardwareFieldColourST : public HardwareField
{
public:
    HardwareFieldColourST(uint32_t addr) :
        m_qtColour(0xffffffff)
    {
        m_memAddress = addr;
    }

    virtual bool Update(const TargetModel* pTarget) override;
    virtual bool GetBrush(QBrush& res) override;
private:
    uint32_t        m_qtColour;
};

//-----------------------------------------------------------------------------
HardwareBase::~HardwareBase()
{
    for (int i = 0; i < m_children.size(); ++i)
        delete m_children[i];
}

HardwareHeader::~HardwareHeader()
{

}

//-----------------------------------------------------------------------------
bool HardwareFieldRegEnum::Update(const TargetModel* pTarget)
{
    QString str;
    if (!GetField(pTarget, m_def, str))
        return false;       // Wrong memory

    m_changed = m_text != str;
    m_text = str;
    return true;
}

//-----------------------------------------------------------------------------
bool HardwareFieldRegBinary16::Update(const TargetModel* pTarget)
{
    QString str;
    if (!GetRegBinary16(pTarget, m_addr, str))
        return false;       // Wrong memory

    m_changed = m_text != str;
    m_text = str;
    return true;
}

//-----------------------------------------------------------------------------
bool HardwareFieldRegSigned16::Update(const TargetModel* pTarget)
{
    QString str;
    if (!GetRegSigned16(pTarget, m_addr, str))
        return false;       // Wrong memory

    m_changed = m_text != str;
    m_text = str;
    return true;
}

//-----------------------------------------------------------------------------
bool HardwareFieldMultiField::Update(const TargetModel* pTarget)
{
    QString res;
    QTextStream ref(&res);
    const stgen::FieldDef** pDef = m_pDefs;
    for (; *pDef; ++pDef)
    {
        const stgen::FieldDef* pCurrDef = *pDef;
        if (!HasAddressMulti(pTarget, pCurrDef->regAddr, 1))
            return false;
        uint32_t regVal = ReadAddressMulti(pTarget, pCurrDef->regAddr, 1);
        uint32_t extracted = (regVal >> pCurrDef->shift) & pCurrDef->mask;

        if (pCurrDef->mask == 1 && !pCurrDef->strings)
        {
            // Single bit
            if (extracted)
            {
                ref << (*pDef)->name << " ";
            }
        }
        else {
            if (pCurrDef->strings)
            {
                const char* str = stgen::GetString(pCurrDef->strings, extracted);
                ref << pCurrDef->name << "=" << str << " ";
            }
            else
            {
                ref << pCurrDef->name << "=" << extracted << " ";
            }
        }
    }
    m_changed = m_text != res;
    m_text = res;
    return true;
}

//-----------------------------------------------------------------------------
HardwareFieldAddr::HardwareFieldAddr(HardwareFieldAddr::Type type, uint32_t address) :
    m_type(type),
    m_address(address)
{
}

//-----------------------------------------------------------------------------
HardwareFieldAddr::HardwareFieldAddr(uint32_t address) :
    m_type(BasePage),
    m_address(address)
{
}

//-----------------------------------------------------------------------------
HardwareFieldAddr::~HardwareFieldAddr()
{
}

//-----------------------------------------------------------------------------
bool HardwareFieldAddr::Update(const TargetModel* pTarget)
{
    const Memory* memVid  = pTarget->GetMemory(MemorySlot::kHardwareWindowVideo);
    const Memory* memBase = pTarget->GetMemory(MemorySlot::kBasePage);
    const Memory* memBlit = pTarget->GetMemory(MemorySlot::kHardwareWindowBlitter);
    const Memory* memMfp = pTarget->GetMemory(MemorySlot::kHardwareWindowMfpVecs);
    const Memory* memDma = pTarget->GetMemory(MemorySlot::kHardwareWindowDmaSnd);

    uint32_t address = 0;
    bool valid = false;
    switch (m_type)
    {
    case Type::ScreenBase:
        if (!memVid)
            break;
        valid = (HardwareST::GetVideoBase(*memVid, pTarget->GetMachineType(), address)); break;
    case Type::ScreenCurr:
        if (!memVid)
            break;
        valid = (HardwareST::GetVideoCurrent(*memVid, address)); break;
    case Type::BltSrc:
        if (!memBlit)
            break;
        valid = (HardwareST::GetBlitterSrc(*memBlit, pTarget->GetMachineType(), address)); break;
    case Type::BltDst:
        if (!memBlit)
            break;
        valid = (HardwareST::GetBlitterDst(*memBlit, pTarget->GetMachineType(), address)); break;
    case Type::DMASndStart:
        if (!memDma)
            break;
        valid = (HardwareST::GetDmaStart(*memDma, pTarget->GetMachineType(), address)); break;
    case Type::DMASndCurr:
        if (!memDma)
            break;
        valid = (HardwareST::GetDmaCurr(*memDma, pTarget->GetMachineType(), address)); break;
    case Type::DMASndEnd:
        if (!memDma)
            break;
        valid = (HardwareST::GetDmaEnd(*memDma, pTarget->GetMachineType(), address)); break;
    case Type::BasePage:
        if (!memBase)
            break;
        valid = memBase->ReadCpuMulti(m_address, 4, address);
        break;
    case Type::Mfp:
        {
            if (!memMfp)
                break;
            {
                uint32_t base = memMfp->GetAddress();
                valid = memMfp->ReadCpuMulti(base + m_address * 4, 4, address);
            }
        }
        break;
    }
    if (valid)
    {
        address &= 0xffffff;
        m_memAddress = address;
        QString str = QString::asprintf("$%08x", address);
        QString sym = DescribeSymbol(pTarget->GetSymbolTable(), address);
        if (!sym.isEmpty())
            str += " (" + sym + ")";
        m_changed = m_text != str;
        m_text = str;
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
bool HardwareFieldYm::Update(const TargetModel* pTarget)
{
    uint8_t val = pTarget->GetYm().m_regs[m_index];
    QString str = QString::asprintf("%u ($%x)", val, val);
    m_changed = m_text != str;
    m_text = str;
    return true;
}

//-----------------------------------------------------------------------------
bool HardwareFieldYmPeriod::Update(const TargetModel *pTarget)
{
    uint16_t valLo = (pTarget->GetYm().m_regs[m_index]);
    uint16_t valHi = (pTarget->GetYm().m_regs[m_index + 1]);
    uint16_t mask = 0xfff;
    double clock = 2000000.0;

    // Divisors are taken from the Datasheet.
    double divisor = clock / 16;

    if (m_index == Regs::YM_PERIOD_ENV_LO)
    {
        divisor = clock / 256;
        mask = 0xffff;
    }
    else if (m_index == Regs::YM_PERIOD_NOISE)
    {
        mask = 0x1f;
    }

    // Special cases
    uint32_t val = (valHi * 256U) | valLo;
    val &= mask;

    double hertz = divisor / (val ? val : 1);
    QString str = QString::asprintf("$%03x  Approx %.0fHz", val, hertz);
    m_changed = m_text != str;
    m_text = str;
    return true;
}

//-----------------------------------------------------------------------------
bool HardwareFieldYmEnvShape::Update(const TargetModel *pTarget)
{
    uint16_t val = (pTarget->GetYm().m_regs[Regs::YM_PERIOD_ENV_SHAPE]);
    const char* pString = Regs::GetString(static_cast<Regs::ENV_SHAPE>(val));
    QString str = pString;
    m_changed = m_text != str;
    m_text = str;
    return true;
}

//-----------------------------------------------------------------------------
bool HardwareFieldYmMixer::Update(const TargetModel *pTarget)
{
    uint32_t val = (pTarget->GetYm().m_regs[Regs::YM_MIXER]);

    QString str;
    if (!Regs::GetField_YM_MIXER_TONE_A_OFF(val))
        str += "SQUARE_A ";
    if (!Regs::GetField_YM_MIXER_TONE_B_OFF(val))
        str += "SQUARE_B ";
    if (!Regs::GetField_YM_MIXER_TONE_C_OFF(val))
        str += "SQUARE_C ";

    if (!Regs::GetField_YM_MIXER_NOISE_A_OFF(val))
        str += "NOISE_A ";
    if (!Regs::GetField_YM_MIXER_NOISE_B_OFF(val))
        str += "NOISE_B ";
    if (!Regs::GetField_YM_MIXER_NOISE_C_OFF(val))
        str += "NOISE_C ";

    m_changed = m_text != str;
    m_text = str;
    return true;
}

//-----------------------------------------------------------------------------
bool HardwareFieldYmVolume::Update(const TargetModel *pTarget)
{
    uint16_t val = pTarget->GetYm().m_regs[m_index];
    uint8_t squareVol = Regs::GetField_YM_VOLUME_A_VOL(val);
    bool useEnv = Regs::GetField_YM_VOLUME_A_ENVELOPE(val);

    QString str = QString::asprintf("Square Vol = %u%s",
                                    squareVol,
                                    useEnv ? " + ENVELOPE" : "");
    m_changed = m_text != str;
    m_text = str;
    return true;
}

//-----------------------------------------------------------------------------
HardwareBitmap::HardwareBitmap(Session *pSession)
{
    m_pImage = new NonAntiAliasImage(nullptr, pSession);
}

HardwareBitmap::~HardwareBitmap()
{
    delete m_pImage;
}

//-----------------------------------------------------------------------------
// TODO: this code isn't used now that we switched to a table view!
bool HardwareBitmapBlitterHalftone::Update(const TargetModel *pTarget)
{
    const Memory& mem = *pTarget->GetMemory(MemorySlot::kHardwareWindowBlitter);
    uint32_t address = Regs::BLT_HALFTONE_0;

    if (!mem.HasCpuRange(address, 16 * 2U))
        return false;

    // Create a temporary image
    Memory tmpMem(MEM_CPU, address, 16 * 2U);
    for (uint32_t i = 0; i < 32; ++i)
    {
        uint8_t val;
        mem.ReadCpuByte(address + i, val);
        tmpMem.Set(i,val);
    }

    MemoryBitmap::Palette pal;
    pal.resize(2U);
    pal[0] = 0xffffffff;
    pal[1] = 0xff000000;
    m_pImage->m_bitmap.Set1Plane(pal, 2U, 16, &tmpMem);
    return true;
}


//-----------------------------------------------------------------------------
bool HardwareFieldColourST::Update(const TargetModel *pTarget)
{
    const Memory* memVid  = pTarget->GetMemory(MemorySlot::kHardwareWindowVideo);
    if (!memVid)
        return false;
    uint32_t val;
    memVid->ReadCpuMulti(m_memAddress, 2, val);

    QString str = QString::asprintf("$%04x", val);
    m_changed = m_text != str;
    m_text = str;
    HardwareST::GetColour(val, pTarget->GetMachineType(), m_qtColour);
    return true;
}

bool HardwareFieldColourST::GetBrush(QBrush &res)
{
    res.setStyle(Qt::BrushStyle::SolidPattern);
    res.setColor(QColor(m_qtColour));
    return true;
}


//-----------------------------------------------------------------------------
// HardwareTreeModel
//-----------------------------------------------------------------------------
HardwareTreeModel::HardwareTreeModel(QObject *parent, TargetModel* pTargetModel)
    : QAbstractItemModel(parent),
      m_pTargetModel(pTargetModel)
{
}

HardwareTreeModel::~HardwareTreeModel()
{
    delete rootItem;
}

int HardwareTreeModel::columnCount(const QModelIndex &/*parent*/) const
{
    return 2;
}

QVariant HardwareTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    HardwareBase *item = static_cast<HardwareBase*>(index.internalPointer());
    if (role == Qt::FontRole)
    {
        if (item->isHeader())
            return m_fontBold;

        if (index.column() == 1 && item->m_changed)
            return m_fontBold;

        return m_font;
    }

    if (role == Qt::DisplayRole)
    {
        switch (index.column())
        {
        case 0:
            return item->m_title;
        case 1:
            return item->m_text;
        }
    }

    if (role == Qt::ToolTipRole)
    {
        return item->m_tooltip;
    }

    // Handle colours
    if (m_pTargetModel->IsConnected() && index.column() == 1)
    {
        if (role == Qt::BackgroundRole)
        {
            // Returns QBrush
            QBrush br;
            if (item->GetBrush(br))
                return br;
        }

        if (role == Qt::ForegroundRole)
        {
            QBrush br;
            if (item->GetBrush(br))
            {
                // Choose white or black with suitable contrast
                if (br.color().red() > 160 || br.color().green() > 160)
                    br.setColor(Qt::black);
                else
                    br.setColor(Qt::white);
                return br;
            }
        }
    }

    return QVariant();
}

Qt::ItemFlags HardwareTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return QAbstractItemModel::flags(index);
}

QVariant HardwareTreeModel::headerData(int section, Qt::Orientation orientation,
                               int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        switch (section)
        {
        case 0:
            return QString("Name");
        case 1:
            return QString("Value");
        }
    }
    return QVariant();
}

QModelIndex HardwareTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    HardwareBase *parentItem;

    if (!parent.isValid())
        parentItem = rootItem;
    else
        parentItem = static_cast<HardwareBase*>(parent.internalPointer());

    HardwareBase *childItem = parentItem->m_children[row];
    if (childItem)
        return createIndex(row, column, childItem);
    return QModelIndex();
}

QModelIndex HardwareTreeModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();

    HardwareBase *childItem = static_cast<HardwareBase*>(index.internalPointer());
    HardwareBase *parentItem = childItem->m_pParent;

    if (parentItem == rootItem)
        return QModelIndex();

    return createIndex(parentItem->m_rowIndex, 0, parentItem);
}

int HardwareTreeModel::rowCount(const QModelIndex &parent) const
{
    HardwareBase *parentItem;
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        parentItem = rootItem;
    else
        parentItem = static_cast<HardwareBase*>(parent.internalPointer());

    return parentItem->m_children.size();
}

void HardwareTreeModel::dataChanged2(HardwareBase* pField)
{
    QModelIndex ind = createIndex(pField->m_rowIndex, 1, pField);
    emit dataChanged(ind, ind);
}

void HardwareTreeModel::UpdateSettings(const Session::Settings &settings)
{
    m_font = settings.m_font;
    m_fontBold = m_font;
    m_fontBold.setBold(true);
}

QModelIndex HardwareTreeModel::createIndex2(HardwareBase *pItem) const
{
    QModelIndex ind = createIndex(pItem->m_rowIndex, 0, pItem);
    return ind;
}

//-----------------------------------------------------------------------------
static void AddClipboardText(QTextStream& ref, HardwareBase* pNode, int indent)
{
    for (int i = 0; i < indent; ++i)
        ref << "\t";

    ref.setFieldAlignment(QTextStream::FieldAlignment::AlignLeft);
    ref.setFieldWidth(28);
    ref << pNode->m_title;
    ref.setFieldWidth(0);
    ref << "\t\t" << pNode->m_text << Format::endl;

    for (HardwareBase* pChild : pNode->m_children)
        AddClipboardText(ref, pChild, indent + 1);
}

//-----------------------------------------------------------------------------
// HardwareTreeView
//-----------------------------------------------------------------------------
HardwareTreeView::HardwareTreeView(QWidget *parent, Session* pSession, HardwareTreeModel* pModel) :
    QTreeView(parent),
    m_pSession(pSession),
    m_pModel(pModel)
{
    setModel(m_pModel);

    m_pCopyAction = new QAction(tr("Copy All"), this);
    m_pCopyAction->setShortcut(QKeySequence::Copy);
    m_pCopyAction->setShortcutContext(Qt::WidgetShortcut);
    this->addAction(m_pCopyAction); // this is needed for keyboard access

    connect(m_pCopyAction,   &QAction::triggered,  this, &HardwareTreeView::copyToClipboard);
}

//-----------------------------------------------------------------------------
void HardwareTreeView::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex ind = indexAt(event->pos());
    if (!ind.isValid())
        return;

    HardwareBase* item = static_cast<HardwareBase*>(ind.internalPointer());
    QMenu menu(this);
    uint32_t addr = item->m_memAddress;
    if (addr != ~0U)
    {
        m_showAddressMenu.Set("Address", m_pSession, MEM_CPU, addr);
        m_showAddressMenu.AddTo(&menu);

        // Run it
    }

    menu.addAction(m_pCopyAction);
    menu.exec(event->globalPos());
}

//-----------------------------------------------------------------------------
void HardwareTreeView::copyToClipboard()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    QString newText;

    QTextStream ref(&newText);
    AddClipboardText(ref, m_pModel->rootItem, 0);

    clipboard->setText(newText);
}

//-----------------------------------------------------------------------------
// HardwareWindow
//-----------------------------------------------------------------------------
HardwareWindow::HardwareWindow(QWidget *parent, Session* pSession) :
    QDockWidget(parent),
    m_pSession(pSession),
    m_pTargetModel(pSession->m_pTargetModel),
    m_pDispatcher(pSession->m_pDispatcher),
    m_videoMem(MEM_CPU, 0, 0),
    m_mfpMem(MEM_CPU, 0, 0),
    m_flushUid(-1)
{
    this->setWindowTitle("Hardware");
    setObjectName("Hardware");

    HardwareBase* m_pRoot = new HardwareBase();
    m_pRoot->m_title = "Hardware Window";
    m_pModel = new HardwareTreeModel(this, m_pTargetModel);
    m_pModel->rootItem = m_pRoot;

    HardwareHeader* pExpVec = new HardwareHeader("Vectors", "");
    HardwareHeader* pExpVecExceptions = new HardwareHeader("Exception Vectors", "");
    HardwareHeader* pExpVecAutos = new HardwareHeader("Auto-Vectors", "");
    HardwareHeader* pExpVecTraps = new HardwareHeader("Trap Vectors", "");
    HardwareHeader* pExpVecMfp = new HardwareHeader("MFP Vectors", "");

    HardwareHeader* pExpMmu = new HardwareHeader("MMU", "Memory Management Unit");
    HardwareHeader* pExpVideo = new HardwareHeader("Shifter/Glue", "Video");
    HardwareHeader* pExpMfp = new HardwareHeader("MFP 68901", "Multi-Function Peripheral");
    HardwareHeader* pExpYm = new HardwareHeader("YM/PSG", "Soundchip");
    HardwareHeader* pExpBlt = new HardwareHeader("Blitter", "");
    HardwareHeader* pExpBltHalftone = new HardwareHeader("Halftone RAM", "");
    HardwareHeader* pExpDmaSnd = new HardwareHeader("DMA Sound", "");
    m_pRoot->AddChild(pExpVec);

    m_pRoot->AddChild(pExpMmu);
    m_pRoot->AddChild(pExpVideo);
    m_pRoot->AddChild(pExpMfp);
    m_pRoot->AddChild(pExpYm);
    m_pRoot->AddChild(pExpBlt);
    m_pRoot->AddChild(pExpDmaSnd);

    // ===== Vectors ====
    addShared(pExpVecExceptions, "Bus Error",           new HardwareFieldAddr(0x8));
    addShared(pExpVecExceptions, "Address Eror",        new HardwareFieldAddr(0xc));
    addShared(pExpVecExceptions, "Illegal Instruction", new HardwareFieldAddr(0x10));
    addShared(pExpVecExceptions, "Zero Divide",         new HardwareFieldAddr(0x14));
    addShared(pExpVecExceptions, "CHK/CHK2",            new HardwareFieldAddr(0x18));
    addShared(pExpVecExceptions, "TRAPcc, TRAPV",       new HardwareFieldAddr(0x1c));
    addShared(pExpVecExceptions, "Privilege Violation", new HardwareFieldAddr(0x20));
    addShared(pExpVecExceptions, "Trace",               new HardwareFieldAddr(0x24));
    addShared(pExpVecExceptions, "Line-A",              new HardwareFieldAddr(0x28));
    addShared(pExpVecExceptions, "Line-F",              new HardwareFieldAddr(0x2c));
    addShared(pExpVecExceptions, "Spurious Interrupt",  new HardwareFieldAddr(0x60));
    pExpVec->AddChild(pExpVecExceptions);

    addShared(pExpVecAutos, "HBL",      new HardwareFieldAddr(0x68));
    addShared(pExpVecAutos, "VBL",      new HardwareFieldAddr(0x70));
    pExpVec->AddChild(pExpVecAutos);

    addShared(pExpVecTraps, "Trap #0",             new HardwareFieldAddr(0x80));
    addShared(pExpVecTraps, "Trap #1 (GemDOS)",    new HardwareFieldAddr(0x84));
    addShared(pExpVecTraps, "Trap #2 (AES/VDI)",   new HardwareFieldAddr(0x88));
    addShared(pExpVecTraps, "Trap #3",             new HardwareFieldAddr(0x8c));
    addShared(pExpVecTraps, "Trap #4",             new HardwareFieldAddr(0x90));
    addShared(pExpVecTraps, "Trap #5",             new HardwareFieldAddr(0x94));
    addShared(pExpVecTraps, "Trap #6",             new HardwareFieldAddr(0x98));
    addShared(pExpVecTraps, "Trap #7",             new HardwareFieldAddr(0x9c));
    addShared(pExpVecTraps, "Trap #8",             new HardwareFieldAddr(0xa0));
    addShared(pExpVecTraps, "Trap #9",             new HardwareFieldAddr(0xa4));
    addShared(pExpVecTraps, "Trap #10",            new HardwareFieldAddr(0xa8));
    addShared(pExpVecTraps, "Trap #11",            new HardwareFieldAddr(0xac));
    addShared(pExpVecTraps, "Trap #12",            new HardwareFieldAddr(0xb0));
    addShared(pExpVecTraps, "Trap #13 (BIOS)",     new HardwareFieldAddr(0xb4));
    addShared(pExpVecTraps, "Trap #14 (XBIOS)",    new HardwareFieldAddr(0xb8));
    addShared(pExpVecTraps, "Trap #15",            new HardwareFieldAddr(0xbc));
    pExpVec->AddChild(pExpVecTraps);

    addShared(pExpVecMfp, "Centronics busy",          new HardwareFieldAddr(HardwareFieldAddr::Mfp, 0));
    addShared(pExpVecMfp, "RS-232 DCD",               new HardwareFieldAddr(HardwareFieldAddr::Mfp, 1));
    addShared(pExpVecMfp, "RS-232 CTS",               new HardwareFieldAddr(HardwareFieldAddr::Mfp, 2));
    addShared(pExpVecMfp, "Blitter done",             new HardwareFieldAddr(HardwareFieldAddr::Mfp, 3));
    addShared(pExpVecMfp, "Timer D",                  new HardwareFieldAddr(HardwareFieldAddr::Mfp, 4));
    addShared(pExpVecMfp, "Timer C",                  new HardwareFieldAddr(HardwareFieldAddr::Mfp, 5));
    addShared(pExpVecMfp, "Keyboard/MIDI (ACIA)",     new HardwareFieldAddr(HardwareFieldAddr::Mfp, 6));
    addShared(pExpVecMfp, "FDC/HDC",                  new HardwareFieldAddr(HardwareFieldAddr::Mfp, 7));
    addShared(pExpVecMfp, "Timer B",                  new HardwareFieldAddr(HardwareFieldAddr::Mfp, 8));
    addShared(pExpVecMfp, "Send Error",               new HardwareFieldAddr(HardwareFieldAddr::Mfp, 9));
    addShared(pExpVecMfp, "Send buffer empty",        new HardwareFieldAddr(HardwareFieldAddr::Mfp, 10));
    addShared(pExpVecMfp, "Receive error",            new HardwareFieldAddr(HardwareFieldAddr::Mfp, 11));
    addShared(pExpVecMfp, "Receive buffer full",      new HardwareFieldAddr(HardwareFieldAddr::Mfp, 12));
    addShared(pExpVecMfp, "Timer A",                  new HardwareFieldAddr(HardwareFieldAddr::Mfp, 13));
    addShared(pExpVecMfp, "RS-232 Ring detect",       new HardwareFieldAddr(HardwareFieldAddr::Mfp, 14));
    addShared(pExpVecMfp, "GPI7 - Monochrome Detect", new HardwareFieldAddr(HardwareFieldAddr::Mfp, 15));
    pExpVec->AddChild(pExpVecMfp);

    // ===== MMU ====
    addField(pExpMmu,  "Bank 0",                   Regs::g_fieldDef_MMU_CONFIG_BANK0);
    addField(pExpMmu,  "Bank 1",                   Regs::g_fieldDef_MMU_CONFIG_BANK1);

    // ===== VIDEO ====
    addField(pExpVideo,  "Resolution",             Regs::g_fieldDef_VID_SHIFTER_RES_RES);
    addField(pExpVideo,  "Sync Rate",              Regs::g_fieldDef_VID_SYNC_MODE_RATE);
    addShared(pExpVideo, "Screen Base Address",    new HardwareFieldAddr(HardwareFieldAddr::ScreenBase));
    addShared(pExpVideo, "Current Read Address",   new HardwareFieldAddr(HardwareFieldAddr::ScreenCurr));

    addField(pExpVideo, "Horizontal Scroll (STE)", Regs::g_fieldDef_VID_HORIZ_SCROLL_STE_PIXELS);
    addField(pExpVideo, "Scanline offset (STE)",   Regs::g_fieldDef_VID_SCANLINE_OFFSET_STE_ALL);

    addShared(pExpVideo, "Colour #0",              new HardwareFieldColourST(0xff8240));
    addShared(pExpVideo, "Colour #1",              new HardwareFieldColourST(0xff8242));
    addShared(pExpVideo, "Colour #2",              new HardwareFieldColourST(0xff8244));
    addShared(pExpVideo, "Colour #3",              new HardwareFieldColourST(0xff8246));
    addShared(pExpVideo, "Colour #4",              new HardwareFieldColourST(0xff8248));
    addShared(pExpVideo, "Colour #5",              new HardwareFieldColourST(0xff824a));
    addShared(pExpVideo, "Colour #6",              new HardwareFieldColourST(0xff824c));
    addShared(pExpVideo, "Colour #7",              new HardwareFieldColourST(0xff824e));
    addShared(pExpVideo, "Colour #8",              new HardwareFieldColourST(0xff8250));
    addShared(pExpVideo, "Colour #9",              new HardwareFieldColourST(0xff8252));
    addShared(pExpVideo, "Colour #10",             new HardwareFieldColourST(0xff8254));
    addShared(pExpVideo, "Colour #11",             new HardwareFieldColourST(0xff8256));
    addShared(pExpVideo, "Colour #12",             new HardwareFieldColourST(0xff8258));
    addShared(pExpVideo, "Colour #13",             new HardwareFieldColourST(0xff825a));
    addShared(pExpVideo, "Colour #14",             new HardwareFieldColourST(0xff825c));
    addShared(pExpVideo, "Colour #15",             new HardwareFieldColourST(0xff825e));

    // ===== MFP ====
    addField(pExpMfp, "Parallel Port Data",           Regs::g_fieldDef_MFP_GPIP_ALL);
    addMultiField(pExpMfp, "Active Edge low->high",   Regs::g_regFieldsDef_MFP_AER);
    addMultiField(pExpMfp, "Data Direction output",   Regs::g_regFieldsDef_MFP_DDR);

    addMultiField(pExpMfp, "IERA (Enable)",           Regs::g_regFieldsDef_MFP_IERA);
    addMultiField(pExpMfp, "IMRA (Mask)",             Regs::g_regFieldsDef_MFP_IMRA);
    addMultiField(pExpMfp, "IPRA (Pending)",          Regs::g_regFieldsDef_MFP_IPRA);
    addMultiField(pExpMfp, "ISRA (Service)",          Regs::g_regFieldsDef_MFP_ISRA);

    addMultiField(pExpMfp, "IERB (Enable)",           Regs::g_regFieldsDef_MFP_IERB);
    addMultiField(pExpMfp, "IMRB (Mask)",             Regs::g_regFieldsDef_MFP_IMRB);
    addMultiField(pExpMfp, "IPRB (Pending)",          Regs::g_regFieldsDef_MFP_IPRB);
    addMultiField(pExpMfp, "ISRB (Service)",          Regs::g_regFieldsDef_MFP_ISRB);

    addField(pExpMfp, "Vector Base offset",           Regs::g_fieldDef_MFP_VR_VEC_BASE_OFFSET);
    addField(pExpMfp, "End-of-Interrupt",             Regs::g_fieldDef_MFP_VR_ENDINT);


    addField(pExpMfp,  "Timer A Control Mode",        Regs::g_fieldDef_MFP_TACR_MODE_TIMER_A);
    addField(pExpMfp,  "Timer A Data",                Regs::g_fieldDef_MFP_TADR_ALL);
    addShared(pExpMfp, "Timer A Vector",              new HardwareFieldAddr(HardwareFieldAddr::Mfp, 13));
    addField(pExpMfp,  "Timer B Control Mode",        Regs::g_fieldDef_MFP_TBCR_MODE_TIMER_B);
    addField(pExpMfp,  "Timer B Data",                Regs::g_fieldDef_MFP_TBDR_ALL);
    addShared(pExpMfp, "Timer B Vector",              new HardwareFieldAddr(HardwareFieldAddr::Mfp, 8));
    addField(pExpMfp,  "Timer C Control Mode",        Regs::g_fieldDef_MFP_TCDCR_MODE_TIMER_C);
    addField(pExpMfp,  "Timer C Data",                Regs::g_fieldDef_MFP_TCDR_ALL);
    addShared(pExpMfp, "Timer C Vector",              new HardwareFieldAddr(HardwareFieldAddr::Mfp, 5));
    addField(pExpMfp,  "Timer D Control Mode",        Regs::g_fieldDef_MFP_TCDCR_MODE_TIMER_D);
    addField(pExpMfp,  "Timer D Data",                Regs::g_fieldDef_MFP_TDDR_ALL);
    addShared(pExpMfp, "Timer D Vector",              new HardwareFieldAddr(HardwareFieldAddr::Mfp, 4));

    addField(pExpMfp, "Sync Char",                    Regs::g_fieldDef_MFP_SCR_ALL);
    addMultiField(pExpMfp, "USART Control",           Regs::g_regFieldsDef_MFP_UCR);
    addMultiField(pExpMfp, "USART RX Status",         Regs::g_regFieldsDef_MFP_RSR);
    addMultiField(pExpMfp, "USART TX Status",         Regs::g_regFieldsDef_MFP_TSR);
    addField(pExpMfp, "USART Data",                   Regs::g_fieldDef_MFP_UDR_ALL);

    // ===== YM ====
    addShared(pExpYm, "Period A",     new HardwareFieldYmPeriod(Regs::YM_PERIOD_A_LO));
    addShared(pExpYm, "Period B",     new HardwareFieldYmPeriod(Regs::YM_PERIOD_B_LO));
    addShared(pExpYm, "Period C",     new HardwareFieldYmPeriod(Regs::YM_PERIOD_C_LO));
    addShared(pExpYm, "Noise Period", new HardwareFieldYmPeriod(Regs::YM_PERIOD_NOISE));
    addShared(pExpYm, "Mixer",        new HardwareFieldYmMixer());
    addShared(pExpYm, "Volume A",     new HardwareFieldYmVolume(Regs::YM_VOLUME_A));
    addShared(pExpYm, "Volume B",     new HardwareFieldYmVolume(Regs::YM_VOLUME_B));
    addShared(pExpYm, "Volume C",     new HardwareFieldYmVolume(Regs::YM_VOLUME_C));
    addShared(pExpYm, "Env Period",   new HardwareFieldYmPeriod(Regs::YM_PERIOD_ENV_LO));
    addShared(pExpYm, "Env Shape",    new HardwareFieldYmEnvShape());
    addShared(pExpYm, "Port A",       new HardwareFieldYm(Regs::YM_PORT_A));
    addShared(pExpYm, "Port B",       new HardwareFieldYm(Regs::YM_PORT_B));

    // ===== BLITTER ====
    // TODO these need sign expansion etc

    // Halftone
    addRegBinary16(pExpBltHalftone, "Halftone 0",   Regs::BLT_HALFTONE_0);
    addRegBinary16(pExpBltHalftone, "Halftone 1",   Regs::BLT_HALFTONE_1);
    addRegBinary16(pExpBltHalftone, "Halftone 2",   Regs::BLT_HALFTONE_2);
    addRegBinary16(pExpBltHalftone, "Halftone 3",   Regs::BLT_HALFTONE_3);
    addRegBinary16(pExpBltHalftone, "Halftone 4",   Regs::BLT_HALFTONE_4);
    addRegBinary16(pExpBltHalftone, "Halftone 5",   Regs::BLT_HALFTONE_5);
    addRegBinary16(pExpBltHalftone, "Halftone 6",   Regs::BLT_HALFTONE_6);
    addRegBinary16(pExpBltHalftone, "Halftone 7",   Regs::BLT_HALFTONE_7);
    addRegBinary16(pExpBltHalftone, "Halftone 8",   Regs::BLT_HALFTONE_8);
    addRegBinary16(pExpBltHalftone, "Halftone 9",   Regs::BLT_HALFTONE_9);
    addRegBinary16(pExpBltHalftone, "Halftone 10",  Regs::BLT_HALFTONE_10);
    addRegBinary16(pExpBltHalftone, "Halftone 11",  Regs::BLT_HALFTONE_11);
    addRegBinary16(pExpBltHalftone, "Halftone 12",  Regs::BLT_HALFTONE_12);
    addRegBinary16(pExpBltHalftone, "Halftone 13",  Regs::BLT_HALFTONE_13);
    addRegBinary16(pExpBltHalftone, "Halftone 14",  Regs::BLT_HALFTONE_14);
    addRegBinary16(pExpBltHalftone, "Halftone 15",  Regs::BLT_HALFTONE_15);
    pExpBlt->AddChild(pExpBltHalftone);

    addRegSigned16( pExpBlt, "Src X Inc",           Regs::BLT_SRC_INC_X);
    addRegSigned16( pExpBlt, "Src Y Inc",           Regs::BLT_SRC_INC_Y);
    addShared(pExpBlt,       "Src Addr",            new HardwareFieldAddr(HardwareFieldAddr::BltSrc));
    addRegSigned16( pExpBlt, "Dst X Inc",           Regs::BLT_DST_INC_X);
    addRegSigned16( pExpBlt, "Dst Y Inc",           Regs::BLT_DST_INC_Y);
    addShared(pExpBlt,       "Dst Addr",            new HardwareFieldAddr(HardwareFieldAddr::BltDst));

    addField(pExpBlt,        "X Count",             Regs::g_fieldDef_BLT_XCOUNT_ALL);
    addField(pExpBlt,        "Y Count",             Regs::g_fieldDef_BLT_YCOUNT_ALL);

    addField(pExpBlt,        "Halftone Op",         Regs::g_fieldDef_BLT_HALFTONE_OP_OP);
    addField(pExpBlt,        "Logical Op",          Regs::g_fieldDef_BLT_LOGICAL_OP_OP);

    addMultiField(pExpBlt,   "Control 1",           Regs::g_regFieldsDef_BLT_CTRL_1);
    addMultiField(pExpBlt,   "Control 2",           Regs::g_regFieldsDef_BLT_CTRL_2);

    addRegBinary16(pExpBlt,  "Endmask 1",           Regs::BLT_ENDMASK_1);
    addRegBinary16(pExpBlt,  "Endmask 2",           Regs::BLT_ENDMASK_2);
    addRegBinary16(pExpBlt,  "Endmask 3",           Regs::BLT_ENDMASK_3);

    // ===== DMA SND ====
    addMultiField(pExpDmaSnd, "DMA Interrupts", Regs::g_regFieldsDef_DMA_BUFFER_INTERRUPTS);
    addMultiField(pExpDmaSnd, "DMA Control",    Regs::g_regFieldsDef_DMA_CONTROL);
    addShared(pExpDmaSnd, "Frame Start",        new HardwareFieldAddr(HardwareFieldAddr::DMASndStart));
    addShared(pExpDmaSnd, "Frame Current",      new HardwareFieldAddr(HardwareFieldAddr::DMASndCurr));
    addShared(pExpDmaSnd, "Frame End",          new HardwareFieldAddr(HardwareFieldAddr::DMASndEnd));
    addMultiField(pExpDmaSnd, "DMA Sound Mode", Regs::g_regFieldsDef_DMA_SND_MODE);

    // Layouts
    QVBoxLayout* pMainLayout = new QVBoxLayout();
    SetMargins(pMainLayout);
    m_pView = new HardwareTreeView(this, m_pSession, m_pModel);
    m_pView->setExpanded(m_pModel->createIndex2(pExpVec), true);
    m_pView->setExpanded(m_pModel->createIndex2(pExpMmu), true);
    m_pView->setExpanded(m_pModel->createIndex2(pExpVideo), true);
    m_pView->setExpanded(m_pModel->createIndex2(pExpMfp), true);
    m_pView->setExpanded(m_pModel->createIndex2(pExpYm), true);
    m_pView->setExpanded(m_pModel->createIndex2(pExpBlt), true);
    m_pView->setExpanded(m_pModel->createIndex2(pExpDmaSnd), true);

    pMainLayout->addWidget(m_pView);
    auto pMainRegion = new QWidget(this);   // whole panel
    pMainRegion->setLayout(pMainLayout);
    setWidget(pMainRegion);

    loadSettings();

    connect(m_pTargetModel,  &TargetModel::connectChangedSignal,   this, &HardwareWindow::connectChanged);
    connect(m_pTargetModel,  &TargetModel::startStopChangedSignal, this, &HardwareWindow::startStopChanged);
    connect(m_pTargetModel,  &TargetModel::memoryChangedSignal,    this, &HardwareWindow::memoryChanged);
    connect(m_pTargetModel,  &TargetModel::flushSignal,            this, &HardwareWindow::flush);

    connect(m_pSession,      &Session::settingsChanged,            this, &HardwareWindow::settingsChanged);

    // Refresh enable state
    connectChanged();

    // Refresh font
    settingsChanged();

    // Call this after settingsChangedSlot so that the font is known
    m_pView->resizeColumnToContents(0);
}

//-----------------------------------------------------------------------------
HardwareWindow::~HardwareWindow()
{

}

//-----------------------------------------------------------------------------
void HardwareWindow::keyFocus()
{
    activateWindow();
    m_pView->setFocus();
}

//-----------------------------------------------------------------------------
void HardwareWindow::loadSettings()
{
    QSettings settings;
    settings.beginGroup("Hardware");

    restoreGeometry(settings.value("geometry").toByteArray());
    settings.endGroup();
}

//-----------------------------------------------------------------------------
void HardwareWindow::saveSettings()
{
    QSettings settings;
    settings.beginGroup("Hardware");

    settings.setValue("geometry", saveGeometry());
    settings.endGroup();
}

//-----------------------------------------------------------------------------
void HardwareWindow::connectChanged()
{
    if (!m_pTargetModel->IsConnected())
    {
        // Disconnect
        // Empty all fields
        for (auto pField : m_fields)
        {
            pField->m_text.clear();
            pField->m_changed = false;
            m_pModel->dataChanged2(pField);
        }
    }
}

//-----------------------------------------------------------------------------
void HardwareWindow::startStopChanged()
{
    if (!m_pTargetModel->IsRunning())
    {
        // Stopped -- request data
        m_pDispatcher->ReadMemory(MemorySlot::kHardwareWindowMmu,     Regs::MMU_CONFIG,    0x1);
        m_pDispatcher->ReadMemory(MemorySlot::kHardwareWindowVideo,   Regs::VID_REG_BASE,  0x70);

        // This one triggers an extra memory request in memoryChangedSlot() for the MFP vectors
        // which are dependent on a register
        m_pDispatcher->ReadMemory(MemorySlot::kHardwareWindowMfp,     Regs::MFP_GPIP,      0x30);
        m_pDispatcher->ReadMemory(MemorySlot::kHardwareWindowBlitter, Regs::BLT_HALFTONE_0,0x40);
        m_pDispatcher->ReadMemory(MemorySlot::kHardwareWindowDmaSnd,  Regs::DMA_SND_BASE,  0x40);
        m_pDispatcher->ReadInfoYm();
    }
}

//-----------------------------------------------------------------------------
void HardwareWindow::flush(const TargetChangedFlags& /*flags*/, uint64_t commandId)
{
    if (commandId == m_flushUid)
    {
        // All commands necessary for the view are available, so update display
        for (auto pField : m_fields)
        {
            if (pField->Update(m_pTargetModel))
                m_pModel->dataChanged2(pField);
        }
    }
}

//-----------------------------------------------------------------------------
void HardwareWindow::memoryChanged(int memorySlot, uint64_t /*commandId*/)
{
    if (memorySlot == MemorySlot::kHardwareWindowMfp)
    {
        const Memory* pMem = m_pTargetModel->GetMemory(MemorySlot::kHardwareWindowMfp);

        // Special case: MFP vector base register
        uint32_t vecBase;
        if (pMem && GetFieldVal(*pMem, Regs::g_fieldDef_MFP_VR_VEC_BASE_OFFSET, vecBase))
        {
            // We combine the upper 4 bits with the lower 4 bits of the MFP interrupt type
            uint32_t vecIndex = vecBase * 16;
            uint32_t vecAddr = vecIndex * 4;

            // Request memory
            m_pDispatcher->ReadMemory(MemorySlot::kHardwareWindowMfpVecs, vecAddr, 4 * 32);

            // Insert the flush.
            // The flush's callback will trigger recalculation of the table model.
            m_flushUid = m_pDispatcher->InsertFlush();
        }
    }
}

//-----------------------------------------------------------------------------
void HardwareWindow::settingsChanged()
{
    m_pModel->UpdateSettings(m_pSession->GetSettings());
}

//-----------------------------------------------------------------------------
void HardwareWindow::addField(HardwareBase* pLayout, const QString& title, const stgen::FieldDef &def)
{
    HardwareFieldRegEnum* pField = new HardwareFieldRegEnum(def);
    addShared(pLayout, title, pField);
}

//-----------------------------------------------------------------------------
void HardwareWindow::addRegBinary16(HardwareBase* pLayout, const QString& title, const uint32_t regAddr)
{
    HardwareFieldRegBinary16* pField = new HardwareFieldRegBinary16(regAddr);
    addShared(pLayout, title, pField);
}

//-----------------------------------------------------------------------------
void HardwareWindow::addRegSigned16(HardwareBase* pLayout, const QString& title, const uint32_t regAddr)
{
    HardwareFieldRegSigned16* pField = new HardwareFieldRegSigned16(regAddr);
    addShared(pLayout, title, pField);
}

//-----------------------------------------------------------------------------
void HardwareWindow::addMultiField(HardwareBase* pLayout, const QString& title, const stgen::FieldDef** defs)
{
    HardwareFieldMultiField* pField = new HardwareFieldMultiField(defs);
    addShared(pLayout, title, pField);
}

//-----------------------------------------------------------------------------
void HardwareWindow::addShared(HardwareBase *pLayout, const QString &title, HardwareField *pField)
{
    m_fields.append(pField);
    pField->m_title = title;
    pLayout->AddChild(pField);
}
