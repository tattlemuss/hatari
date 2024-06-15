#include "graphicsinspector.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QCompleter>
#include <QSpinBox>
#include <QShortcut>
#include <QKeyEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QMenu>

#include <QFontDatabase>
#include <QSettings>
#include <QDebug>
#include <QFileDialog>

#include "../transport/dispatcher.h"
#include "../models/targetmodel.h"
#include "../models/symboltablemodel.h"
#include "../models/stringformat.h"
#include "../models/stringparsers.h"
#include "../models/session.h"
#include "../hardware/regs_st.h"

#include "elidedlabel.h"
#include "quicklayout.h"
#include "symboltext.h"

/* A note on memory requests:

    The code uses a dependency system, since it needs 3 bits of memory
    * the video regs
    * the palette (which might be memory or not
    * the bitmap, (which might depend on registers)

    The bitmap cannot be requested until video regs and palette have been
    updated when necessary.

    Each one of these hsa a "Request" structure to check whether it needs
    re-requesting, or checking if it has been received.

    We flag a request as "dirty" (needs a new request) then call UpdateMemoryRequests
    which sorts out calling the dispatcher. Also when we receive memory
    we clear the request dirty flags and also call UpdateMemoryRequests to
    fetch any dependent memory.

    Finally when all requests are done we can call UpdateImage which creates
    the bitmap and final palette.
*/

static void CreateBitplanePalette(QVector<uint32_t>& palette,
                                  uint32_t col0,
                                  uint32_t col1,
                                  uint32_t col2,
                                  uint32_t col3)
{
    palette.append(0xff000000 + 0                  );
    palette.append(0xff000000 +               +col0);
    palette.append(0xff000000 +          +col1     );
    palette.append(0xff000000 +          +col1+col0);
    palette.append(0xff000000 +      col2          );
    palette.append(0xff000000 +      col2     +col0);
    palette.append(0xff000000 +      col2+col1     );
    palette.append(0xff000000 +      col2+col1+col0);
    palette.append(0xff000000 + col3               );
    palette.append(0xff000000 + col3          +col0);
    palette.append(0xff000000 + col3     +col1     );
    palette.append(0xff000000 + col3     +col1+col0);
    palette.append(0xff000000 + col3+col2          );
    palette.append(0xff000000 + col3+col2     +col0);
    palette.append(0xff000000 + col3+col2+col1     );
    palette.append(0xff000000 + col3+col2+col1+col0);
}

GraphicsInspectorWidget::GraphicsInspectorWidget(QWidget *parent,
                                                 Session* pSession) :
    QDockWidget(parent),
    m_pSession(pSession),
    m_mode(k4Bitplane),
    m_bitmapAddress(0U),
    m_width(20),
    m_height(200),
    m_padding(0),
    m_paletteMode(kRegisters),
    m_cachedResolution(Regs::RESOLUTION::LOW),
    m_annotateRegisters(false)
{
    m_paletteAddress = Regs::VID_PAL_0;

    m_pTargetModel = m_pSession->m_pTargetModel;
    m_pDispatcher = m_pSession->m_pDispatcher;

    QString name("GraphicsInspector");
    this->setObjectName(name);
    this->setWindowTitle(name);
    this->setFocusPolicy(Qt::FocusPolicy::StrongFocus);

    // Make img widget first so that tab order "works"
    m_pImageWidget = new NonAntiAliasImage(this, pSession);

    // Fill out starting mouseover data
    m_mouseInfo = m_pImageWidget->GetMouseInfo();
    m_addressUnderMouse = ~0U;

    // top line
    m_pBitmapAddressLineEdit = new QLineEdit(this);
    m_pLockAddressToVideoCheckBox = new QCheckBox(tr("Use Registers"), this);
    m_pSymbolTableModel = new SymbolTableModel(this, m_pTargetModel->GetSymbolTable());
    QCompleter* pCompl = new QCompleter(m_pSymbolTableModel, this);
    pCompl->setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);
    m_pBitmapAddressLineEdit->setCompleter(pCompl);

    // Second line
    m_pModeComboBox = new QComboBox(this);
    m_pWidthSpinBox = new QSpinBox(this);
    m_pHeightSpinBox = new QSpinBox(this);
    m_pPaddingSpinBox = new QSpinBox(this);
    m_pLockFormatToVideoCheckBox = new QCheckBox(tr("Use Registers"), this);

    m_pModeComboBox->addItem(tr("4 Plane"), Mode::k4Bitplane);
    m_pModeComboBox->addItem(tr("3 Plane"), Mode::k3Bitplane);
    m_pModeComboBox->addItem(tr("2 Plane"), Mode::k2Bitplane);
    m_pModeComboBox->addItem(tr("1 Plane"), Mode::k1Bitplane);

    // Width is now 80 to support the "1280" special ST mode for @troed
    m_pWidthSpinBox->setRange(1, 80);
    m_pWidthSpinBox->setValue(m_width);
    m_pHeightSpinBox->setRange(16, 256);
    m_pHeightSpinBox->setValue(m_height);
    m_pPaddingSpinBox->setRange(0, 8);
    m_pPaddingSpinBox->setValue(m_padding);

    // Third line
    m_pPaletteComboBox = new QComboBox(this);
    m_pPaletteComboBox->addItem(tr("Registers"), kRegisters);
    m_pPaletteComboBox->addItem(tr("Memory..."), kUserMemory);
    m_pPaletteComboBox->addItem(tr("Greyscale"), kGreyscale);
    m_pPaletteComboBox->addItem(tr("Contrast1"), kContrast1);
    m_pPaletteComboBox->addItem(tr("Bitplane0"), kBitplane0);
    m_pPaletteComboBox->addItem(tr("Bitplane1"), kBitplane1);
    m_pPaletteComboBox->addItem(tr("Bitplane2"), kBitplane2);
    m_pPaletteComboBox->addItem(tr("Bitplane3"), kBitplane3);
    m_pPaletteAddressLineEdit = new QLineEdit(this);
    m_pPaletteAddressLineEdit->setCompleter(pCompl);    // use same completer as address box
    m_pInfoLabel = new ElidedLabel("", this);

    // Bottom
    m_pImageWidget->setSizePolicy(QSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding));

    // Layout First line
    QHBoxLayout *hlayout1 = new QHBoxLayout();
    SetMargins(hlayout1);
    hlayout1->setAlignment(Qt::AlignLeft);
    hlayout1->addWidget(new QLabel(tr("Address:"), this));
    hlayout1->addWidget(m_pBitmapAddressLineEdit);
    hlayout1->addWidget(m_pLockAddressToVideoCheckBox);
    QWidget* pContainer1 = new QWidget(this);
    pContainer1->setLayout(hlayout1);

    // Layout Second line
    QString widthTooltip(tr("Width in 16-pixel chunks"));
    QString padTooltip(tr("Line stride padding in bytes"));

    QHBoxLayout *hlayout2 = new QHBoxLayout();
    SetMargins(hlayout2);
    hlayout2->setAlignment(Qt::AlignLeft);
    hlayout2->addWidget(new QLabel(tr("Format:"), this));
    hlayout2->addWidget(m_pModeComboBox);

    QLabel* pWidthLabel = new QLabel(tr("Width:"), this);
    pWidthLabel->setToolTip(widthTooltip);
    m_pWidthSpinBox->setToolTip(widthTooltip);
    hlayout2->addWidget(pWidthLabel);
    hlayout2->addWidget(m_pWidthSpinBox);

    hlayout2->addWidget(new QLabel(tr("Height:"), this));
    hlayout2->addWidget(m_pHeightSpinBox);

    QLabel* pPaddingLabel = new QLabel(tr("Pad:"), this);
    pPaddingLabel->setToolTip(padTooltip);
    m_pPaddingSpinBox->setToolTip(padTooltip);
    hlayout2->addWidget(pPaddingLabel);
    hlayout2->addWidget(m_pPaddingSpinBox);

    hlayout2->addWidget(m_pLockFormatToVideoCheckBox);
    QWidget* pContainer2 = new QWidget(this);
    pContainer2->setLayout(hlayout2);

    // Layout Third line
    QHBoxLayout *hlayout3 = new QHBoxLayout();
    SetMargins(hlayout3);
    hlayout3->setAlignment(Qt::AlignLeft);
    hlayout3->addWidget(new QLabel(tr("Palette:"), this));
    hlayout3->addWidget(m_pPaletteComboBox);
    hlayout3->addWidget(m_pPaletteAddressLineEdit);
    hlayout3->addWidget(m_pInfoLabel);
    QWidget* pContainer3 = new QWidget(this);
    pContainer3->setLayout(hlayout3);

    auto pMainGroupBox = new QWidget(this);
    QVBoxLayout *vlayout = new QVBoxLayout;
    SetMargins(vlayout);
    vlayout->addWidget(pContainer1);
    vlayout->addWidget(pContainer2);
    vlayout->addWidget(pContainer3);
    vlayout->addWidget(m_pImageWidget);
    vlayout->setAlignment(Qt::Alignment(Qt::AlignTop));
    pMainGroupBox->setLayout(vlayout);

    setWidget(pMainGroupBox);
    m_pLockAddressToVideoCheckBox->setChecked(true);
    m_pLockFormatToVideoCheckBox->setChecked(true);

    // Actions
    m_pSaveImageAction = new QAction(tr("Save Image..."), this);
    m_pOverlayMenu = new QMenu("Overlays", this);
    m_pOverlayDarkenAction = new QAction("Darken Image", this);
    m_pOverlayDarkenAction->setCheckable(true);
    m_pOverlayGridAction = new QAction("Grid", this);
    m_pOverlayGridAction->setCheckable(true);
    m_pOverlayZoomAction = new QAction("Zoom", this);
    m_pOverlayZoomAction->setCheckable(true);
    m_pOverlayRegistersAction = new QAction("Address Registers", this);
    m_pOverlayRegistersAction->setCheckable(true);

    m_pOverlayMenu->addAction(m_pOverlayDarkenAction);
    m_pOverlayMenu->addAction(m_pOverlayGridAction);
    m_pOverlayMenu->addAction(m_pOverlayZoomAction);
    m_pOverlayMenu->addAction(m_pOverlayRegistersAction);

    // Keyboard shortcuts
    new QShortcut(QKeySequence("Ctrl+G"),         this, SLOT(gotoClickedSlot()), nullptr, Qt::WidgetWithChildrenShortcut);
    new QShortcut(QKeySequence("Ctrl+L"),         this, SLOT(lockClickedSlot()), nullptr, Qt::WidgetWithChildrenShortcut);

    connect(m_pTargetModel,  &TargetModel::connectChangedSignal,          this, &GraphicsInspectorWidget::connectChanged);
    connect(m_pTargetModel,  &TargetModel::startStopChangedSignal,        this, &GraphicsInspectorWidget::startStopChanged);
    connect(m_pTargetModel,  &TargetModel::startStopChangedSignalDelayed, this, &GraphicsInspectorWidget::startStopDelayedChanged);
    connect(m_pTargetModel,  &TargetModel::memoryChangedSignal,           this, &GraphicsInspectorWidget::memoryChanged);
    connect(m_pTargetModel,  &TargetModel::otherMemoryChangedSignal,      this, &GraphicsInspectorWidget::otherMemoryChanged);
    connect(m_pTargetModel,  &TargetModel::runningRefreshTimerSignal,     this, &GraphicsInspectorWidget::runningRefreshTimer);

    connect(m_pBitmapAddressLineEdit,       &QLineEdit::returnPressed,    this, &GraphicsInspectorWidget::bitmapAddressChanged);
    connect(m_pPaletteAddressLineEdit,      &QLineEdit::returnPressed,    this, &GraphicsInspectorWidget::paletteAddressChanged);
    connect(m_pLockAddressToVideoCheckBox,  &QCheckBox::stateChanged,     this, &GraphicsInspectorWidget::lockAddressToVideoChanged);
    connect(m_pLockFormatToVideoCheckBox,   &QCheckBox::stateChanged,     this, &GraphicsInspectorWidget::lockFormatToVideoChanged);
    connect(m_pLockAddressToVideoCheckBox,  &QCheckBox::stateChanged,     this, &GraphicsInspectorWidget::lockAddressToVideoChanged);

    connect(m_pModeComboBox,    SIGNAL(activated(int)),                   SLOT(modeChangedSlot(int)));  // this is user-changed
    connect(m_pPaletteComboBox, SIGNAL(currentIndexChanged(int)),         SLOT(paletteChangedSlot(int)));
    connect(m_pWidthSpinBox,    SIGNAL(valueChanged(int)),                SLOT(widthChangedSlot(int)));
    connect(m_pHeightSpinBox,   SIGNAL(valueChanged(int)),                SLOT(heightChangedSlot(int)));
    connect(m_pPaddingSpinBox,  SIGNAL(valueChanged(int)),                SLOT(paddingChangedSlot(int)));

    connect(m_pImageWidget,  &NonAntiAliasImage::MouseInfoChanged,        this, &GraphicsInspectorWidget::mouseOverChanged);
    connect(m_pSession,      &Session::addressRequested,                  this, &GraphicsInspectorWidget::RequestBitmapAddress);

    // Context menus
    connect(m_pSaveImageAction,             &QAction::triggered,          this, &GraphicsInspectorWidget::saveImageClicked);
    connect(m_pOverlayDarkenAction,         &QAction::triggered,          this, &GraphicsInspectorWidget::overlayDarkenChanged);
    connect(m_pOverlayGridAction,           &QAction::triggered,          this, &GraphicsInspectorWidget::overlayGridChanged);
    connect(m_pOverlayZoomAction,           &QAction::triggered,          this, &GraphicsInspectorWidget::overlayZoomChanged);
    connect(m_pOverlayRegistersAction,      &QAction::triggered,          this, &GraphicsInspectorWidget::overlayRegistersChanged);

    loadSettings();
    UpdateUIElements();
    DisplayAddress();
}

GraphicsInspectorWidget::~GraphicsInspectorWidget()
{

}

void GraphicsInspectorWidget::keyFocus()
{
    activateWindow();
    m_pImageWidget->setFocus();
}

void GraphicsInspectorWidget::loadSettings()
{
    QSettings settings;
    settings.beginGroup("GraphicsInspector");

    restoreGeometry(settings.value("geometry").toByteArray());
    m_width = settings.value("width", QVariant(20)).toInt();
    m_height = settings.value("height", QVariant(200)).toInt();
    m_padding = settings.value("padding", QVariant(0)).toInt();
    m_mode = static_cast<Mode>(settings.value("mode", QVariant((int)Mode::k4Bitplane)).toInt());
    m_pLockAddressToVideoCheckBox->setChecked(settings.value("lockAddress", QVariant(true)).toBool());
    m_pLockFormatToVideoCheckBox->setChecked(settings.value("lockFormat", QVariant(true)).toBool());

    int palette = settings.value("palette", QVariant((int)Palette::kGreyscale)).toInt();
    m_pPaletteComboBox->setCurrentIndex(palette);

    bool darken = settings.value("darken", QVariant(false)).toBool();
    bool grid = settings.value("grid", QVariant(false)).toBool();
    bool zoom = settings.value("zoom", QVariant(false)).toBool();
    m_pImageWidget->SetDarken(darken);
    m_pImageWidget->SetGrid(grid);
    m_pImageWidget->SetZoom(zoom);
    m_annotateRegisters = settings.value("annotateRegisters", QVariant(false)).toBool();

    UpdateUIElements();
    settings.endGroup();
}

void GraphicsInspectorWidget::saveSettings()
{
    QSettings settings;
    settings.beginGroup("GraphicsInspector");

    settings.setValue("geometry", saveGeometry());
    settings.setValue("width", m_width);
    settings.setValue("height", m_height);
    settings.setValue("padding", m_padding);
    settings.setValue("lockAddress", m_pLockAddressToVideoCheckBox->isChecked());
    settings.setValue("lockFormat", m_pLockFormatToVideoCheckBox->isChecked());
    settings.setValue("mode", static_cast<int>(m_mode));
    settings.setValue("palette", m_pPaletteComboBox->currentIndex());
    settings.setValue("darken", m_pImageWidget->GetDarken());
    settings.setValue("grid", m_pImageWidget->GetGrid());
    settings.setValue("zoom", m_pImageWidget->GetZoom());
    settings.setValue("annotateRegisters", m_annotateRegisters);
    settings.endGroup();
}

void GraphicsInspectorWidget::keyPressEvent(QKeyEvent* ev)
{
    int offset = 0;

    EffectiveData data;
    GetEffectiveData(data);

    int32_t height = GetEffectiveHeight();

    bool shift = (ev->modifiers().testFlag(Qt::KeyboardModifier::ShiftModifier));
    // Handle keyboard shortcuts with scope here, since QShortcut is global
    if (ev->modifiers() == Qt::ControlModifier)
    {
        switch (ev->key())
        {
            case Qt::Key_Space:     KeyboardContextMenu();    return;
            default: break;
        }
    }
    else
    {
        if (ev->key() == Qt::Key::Key_Up)
            offset = shift ? -8 * data.bytesPerLine : -data.bytesPerLine;
        else if (ev->key() == Qt::Key::Key_Down)
            offset = shift ? 8 * data.bytesPerLine : data.bytesPerLine;
        else if (ev->key() == Qt::Key::Key_PageUp)
            offset = -height * data.bytesPerLine;
        else if (ev->key() == Qt::Key::Key_PageDown)
            offset = +height * data.bytesPerLine;
        else if (ev->key() == Qt::Key::Key_Left)
            offset = -2;
        else if (ev->key() == Qt::Key::Key_Right)
            offset = 2;

        // Check nothing is being loaded still before moving
        if (offset && m_requestBitmap.requestId == 0)
        {
            // Going up or down by a small amount is OK
            if (offset > 0 || (int)m_bitmapAddress > -offset)
            {
                m_bitmapAddress += offset;
            }
            else {
                m_bitmapAddress = 0;
            }
            m_pLockAddressToVideoCheckBox->setChecked(false);
            m_requestBitmap.Dirty();
            UpdateMemoryRequests();
            DisplayAddress();

            return;
        }
    }
    QDockWidget::keyPressEvent(ev);
}

void GraphicsInspectorWidget::contextMenuEvent(QContextMenuEvent *event)
{
    ContextMenu(event->globalPos());
}

void GraphicsInspectorWidget::connectChanged()
{
    if (!m_pTargetModel->IsConnected())
    {
        m_pImageWidget->setPixmap(0, 0);
    }
}

void GraphicsInspectorWidget::startStopChanged()
{
    // Nothing happens here except an initial redraw
    update();
}

void GraphicsInspectorWidget::startStopDelayedChanged()
{
    // Request new memory for the view
    if (!m_pTargetModel->IsRunning())
    {
        m_requestPalette.Dirty();
        m_requestBitmap.Dirty();
        m_requestRegs.Dirty();
        UpdateMemoryRequests();
        // Don't clear the Running flag for the widget, do that when the memory arrives
    }
    else {
        // Turn on the running mask slowly
        m_pImageWidget->SetRunning(true);
    }
}

void GraphicsInspectorWidget::memoryChanged(int /*memorySlot*/, uint64_t commandId)
{
    if (commandId == m_requestRegs.requestId)
    {
        m_requestRegs.Clear();

        UpdateFormatFromUI();
        UpdateUIElements();

        // We should have registers now, so use them
        if (m_pLockAddressToVideoCheckBox->isChecked())
            SetBitmapAddressFromVideoRegs();

        // Update cached resolution
        const Memory* pMem = m_pTargetModel->GetMemory(MemorySlot::kGraphicsInspectorVideoRegs);
        if (pMem)
        {
            uint8_t val = 0;
            if (pMem->ReadAddressByte(Regs::VID_SHIFTER_RES, val))
                m_cachedResolution = Regs::GetField_VID_SHIFTER_RES_RES(val);
        }

        // See if bitmap etc can now be requested
        UpdateMemoryRequests();
    }
    else if (commandId == m_requestPalette.requestId)
    {
        m_requestPalette.Clear();
        UpdateMemoryRequests();
    }
    else if (commandId == m_requestBitmap.requestId)
    {
        m_requestBitmap.Clear();
        UpdateMemoryRequests();

        // Clear the running mask, but only if we're really stopped
        if (!m_pTargetModel->IsRunning())
            m_pImageWidget->SetRunning(false);
    }
}

void GraphicsInspectorWidget::bitmapAddressChanged()
{
    std::string expression = m_pBitmapAddressLineEdit->text().toStdString();
    uint32_t addr;
    if (!StringParsers::ParseExpression(expression.c_str(), addr,
                                        m_pTargetModel->GetSymbolTable(),
                                        m_pTargetModel->GetRegs()))
    {
        return;
    }
    m_bitmapAddress = addr;
    m_pLockAddressToVideoCheckBox->setChecked(false);
    UpdateUIElements();

    m_requestBitmap.Dirty();
    UpdateMemoryRequests();
}

void GraphicsInspectorWidget::paletteAddressChanged()
{
    std::string expression = m_pPaletteAddressLineEdit->text().toStdString();
    uint32_t addr;
    if (!StringParsers::ParseExpression(expression.c_str(), addr,
                                        m_pTargetModel->GetSymbolTable(),
                                        m_pTargetModel->GetRegs()))
    {
        return;
    }
    // Re-request palette memory
    m_paletteAddress = addr;
    m_requestPalette.Dirty();
    UpdateMemoryRequests();
}

void GraphicsInspectorWidget::lockAddressToVideoChanged()
{
    bool m_bLockToVideo = m_pLockAddressToVideoCheckBox->isChecked();
    if (m_bLockToVideo)
    {
        // New address, so re-request bitmap memory
        SetBitmapAddressFromVideoRegs();
        m_requestBitmap.Dirty();
        UpdateMemoryRequests();
    }
    UpdateUIElements();
}

void GraphicsInspectorWidget::lockFormatToVideoChanged()
{
    bool m_bLockToVideo = m_pLockFormatToVideoCheckBox->isChecked();
    if (m_bLockToVideo)
    {
        UpdateFormatFromUI();
        m_requestBitmap.Dirty();
        UpdateMemoryRequests();
    }
    UpdateUIElements();
}

void GraphicsInspectorWidget::overlayDarkenChanged()
{
    m_pImageWidget->SetDarken(m_pOverlayDarkenAction->isChecked());
}

void GraphicsInspectorWidget::overlayGridChanged()
{
    m_pImageWidget->SetGrid(m_pOverlayGridAction->isChecked());
}

void GraphicsInspectorWidget::overlayZoomChanged()
{
    m_pImageWidget->SetZoom(m_pOverlayZoomAction->isChecked());
}

void GraphicsInspectorWidget::overlayRegistersChanged()
{
    m_annotateRegisters = m_pOverlayRegistersAction->isChecked();
    UpdateAnnotations();
}

void GraphicsInspectorWidget::modeChangedSlot(int index)
{
    int modeInt = m_pModeComboBox->itemData(index).toInt();
    m_mode = static_cast<Mode>(modeInt);

    // New mode can require different memory size, so re-request bitmap memory
    m_requestBitmap.Dirty();
    UpdateMemoryRequests();
}

void GraphicsInspectorWidget::paletteChangedSlot(int index)
{
    int rawIdx = m_pPaletteComboBox->itemData(index).toInt();
    m_paletteMode = static_cast<Palette>(rawIdx);

    // Ensure pixmap is updated
    m_pImageWidget->setPixmap(GetEffectiveWidth(), GetEffectiveHeight());

    UpdateUIElements();

    // Only certain modes require palette memory re-request
    if (m_paletteMode == kRegisters || m_paletteMode == kUserMemory)
        m_requestPalette.Dirty();

    // This will recalc the image immediately if no requests have been
    // made
    UpdateMemoryRequests();
}

void GraphicsInspectorWidget::gotoClickedSlot()
{
    m_pBitmapAddressLineEdit->setFocus();
}

void GraphicsInspectorWidget::lockClickedSlot()
{
    m_pLockAddressToVideoCheckBox->toggle();
}

void GraphicsInspectorWidget::otherMemoryChanged(uint32_t address, uint32_t size)
{
    // Do a re-request if our memory is touched
    uint32_t ourSize = GetEffectiveHeight() * (GetEffectiveWidth() + GetEffectivePadding()) * BytesPerMode(m_mode);
    if (Overlaps(m_bitmapAddress, ourSize, address, size))
    {
        m_requestBitmap.Dirty();
        UpdateMemoryRequests();
    }
}

void GraphicsInspectorWidget::runningRefreshTimer()
{
    if (m_pTargetModel->IsConnected() && m_pSession->GetSettings().m_liveRefresh)
    {
        m_requestPalette.Dirty();
        m_requestBitmap.Dirty();
        m_requestRegs.Dirty();
        UpdateMemoryRequests();
    }
}

void GraphicsInspectorWidget::widthChangedSlot(int value)
{
    m_width = value;
    m_requestBitmap.Dirty();
    UpdateMemoryRequests();
}

void GraphicsInspectorWidget::heightChangedSlot(int value)
{
    m_height = value;
    m_requestBitmap.Dirty();
    UpdateMemoryRequests();
}

void GraphicsInspectorWidget::paddingChangedSlot(int value)
{
    m_padding = value;
    m_requestBitmap.Dirty();
    UpdateMemoryRequests();
}

void GraphicsInspectorWidget::saveImageClicked()
{
    // Choose output file
    QString filter = "Bitmap files (*.bmp *.png);;All files (*.*);";
    QString filename = QFileDialog::getSaveFileName(
          this,
          tr("Choose image filename"),
          QString(),
          filter);

    if (filename.size() != 0)
        m_pImageWidget->GetImage().save(filename);
}

void GraphicsInspectorWidget::mouseOverChanged()
{
    const NonAntiAliasImage::MouseInfo& info = m_pImageWidget->GetMouseInfo();

    // Take a copy for right-click events
    m_mouseInfo = info;
    m_addressUnderMouse = -1;

    if (info.isValid)
    {
        // We can calculate the memory address here
        uint32_t addr = ~0U;
        EffectiveData data;
        GetEffectiveData(data);
        switch(m_mode)
        {
        case Mode::k1Bitplane:
        case Mode::k2Bitplane:
        case Mode::k3Bitplane:
        case Mode::k4Bitplane:
            addr = m_bitmapAddress + info.y * data.bytesPerLine + (info.x / 16) * BytesPerMode(m_mode);
            break;
        default:
            break;
        }

        QString str;
        QTextStream ref(&str);
        ref << "[" << info.x << ", " << info.y << "]";
        if (info.pixelValue >= 0)
            ref << " Pixel Value: " << info.pixelValue;
        if (addr != ~0U)
        {
            ref << " Address: " << Format::to_hex32(addr);
            QString symText = DescribeSymbol(m_pTargetModel->GetSymbolTable(), addr);
            if (!symText.isEmpty())
                ref << " (" << symText << ")";
        }
        m_pInfoLabel->setText(str);
        m_addressUnderMouse = addr;
    }
    else
    {
        m_pInfoLabel->setText(QString());
    }
}

void GraphicsInspectorWidget::RequestBitmapAddress(Session::WindowType type, int windowIndex, uint32_t address)
{
    if (type != Session::WindowType::kGraphicsInspector)
        return;

    (void) windowIndex;

    // Set the address and override video regs
    m_bitmapAddress = address;
    m_pLockAddressToVideoCheckBox->setChecked(false);
    UpdateUIElements();

    m_requestBitmap.Dirty();
    UpdateMemoryRequests();
    raise();
}

void GraphicsInspectorWidget::UpdateMemoryRequests()
{
    // This is the main entry point for grabbing the data.
    // Trigger a full refresh of registers. We need these to
    // determine size/format in some cases.
    if (!m_pTargetModel->IsConnected())
        return;

    if (!m_requestRegs.isDirty && !m_requestPalette.isDirty && !m_requestBitmap.isDirty)
    {
        // Everything is ready!
        // Update the palette and bitmap ready for display
        UpdateImage();
        return;
    }

    if (m_requestRegs.isDirty || m_requestPalette.isDirty)
    {
        if (m_requestRegs.isDirty && m_requestRegs.requestId == 0)
            m_requestRegs.requestId = m_pDispatcher->ReadMemory(MemorySlot::kGraphicsInspectorVideoRegs, Regs::VID_REG_BASE, 0x70);

        if (m_requestPalette.isDirty && m_requestPalette.requestId == 0)
        {
            if (m_paletteMode == kRegisters)
                m_requestPalette.requestId = m_pDispatcher->ReadMemory(MemorySlot::kGraphicsInspectorPalette, Regs::VID_PAL_0, 0x20);
            else if (m_paletteMode == kUserMemory)
                m_requestPalette.requestId = m_pDispatcher->ReadMemory(MemorySlot::kGraphicsInspectorPalette, m_paletteAddress, 0x20);
            else
                m_requestPalette.Clear();   // we don't need to fetch
        }
        return;
    }

    // We only get here if the other flags are clean
    if (m_requestBitmap.isDirty)
    {
        if (m_requestBitmap.requestId == 0)
        {
            // Request video memory area
            EffectiveData data;
            GetEffectiveData(data);
            m_requestBitmap.requestId = m_pDispatcher->ReadMemory(MemorySlot::kGraphicsInspector, m_bitmapAddress, data.requiredSize);
            DisplayAddress();
        }
        return;
    }
    assert(0);
}

bool GraphicsInspectorWidget::SetBitmapAddressFromVideoRegs()
{
    // Update to current video regs
    const Memory* pVideoRegs = m_pTargetModel->GetMemory(MemorySlot::kGraphicsInspectorVideoRegs);
    if (!pVideoRegs)
        return false;

    uint32_t address;
    if (HardwareST::GetVideoBase(*pVideoRegs, m_pTargetModel->GetMachineType(), address))
    {
        m_bitmapAddress = address;
        m_requestBitmap.Dirty();
        UpdateMemoryRequests();
        return true;
    }
    return false;
}

void GraphicsInspectorWidget::DisplayAddress()
{
    m_pBitmapAddressLineEdit->setText(QString::asprintf("$%x", m_bitmapAddress));
    m_pPaletteAddressLineEdit->setText(QString::asprintf("$%x", m_paletteAddress));
}

void GraphicsInspectorWidget::UpdateFormatFromUI()
{
    // Now we have the registers, we can now video dimensions.
    int width = GetEffectiveWidth();
    int height = GetEffectiveHeight();
    Mode mode = GetEffectiveMode();

    // Set these as current values, so that if we scroll around,
    // they are not lost
    m_width = width;
    m_height = height;
    m_mode = mode;
    UpdateUIElements();
}

void GraphicsInspectorWidget::UpdateImage()
{
    // Colours are ARGB
    m_pImageWidget->m_colours.clear();

    // Now palette
    switch (m_paletteMode)
    {
        case kRegisters:
        case kUserMemory:
        {
            const Memory* pMemOrig = m_pTargetModel->GetMemory(MemorySlot::kGraphicsInspectorPalette);
            if (!pMemOrig)
                return;

            for (uint i = 0; i < 16; ++i)
            {
                uint16_t regVal = pMemOrig->Get(i * 2);
                regVal <<= 8;
                regVal |= pMemOrig->Get(i * 2 + 1);

                uint32_t colour = 0xff000000U;
                HardwareST::GetColour(regVal, m_pTargetModel->GetMachineType(), colour);
                m_pImageWidget->m_colours.append(colour);
            }
            break;
        }
        case kGreyscale:
            for (uint i = 0; i < 16; ++i)
            {
                m_pImageWidget->m_colours.append(0xff000000 + i * 0x101010);
            }
            break;
        case kContrast1:
            // This palette is derived from one of the bitplane palettes in "44"
            CreateBitplanePalette(m_pImageWidget->m_colours, 0x500000*2, 0x224400*2, 0x003322*2, 0x000055*2);
            break;
        case kBitplane0:
            CreateBitplanePalette(m_pImageWidget->m_colours, 0xbbbbbb, 0x220000, 0x2200, 0x22);
            break;
        case kBitplane1:
            CreateBitplanePalette(m_pImageWidget->m_colours, 0x220000, 0xbbbbbb, 0x2200, 0x22);
            break;
        case kBitplane2:
            CreateBitplanePalette(m_pImageWidget->m_colours, 0x220000, 0x2200, 0xbbbbbb, 0x22);
            break;
        case kBitplane3:
            CreateBitplanePalette(m_pImageWidget->m_colours, 0x220000, 0x2200, 0x22, 0xbbbbbb);
            break;
    }

    if (m_paletteMode == kRegisters)
    {
        if (m_cachedResolution == Regs::RESOLUTION::HIGH)
        {
            const Memory* pMem = m_pTargetModel->GetMemory(MemorySlot::kGraphicsInspectorVideoRegs);
            uint32_t colour0 = 0;
            if (pMem && pMem->ReadAddressMulti(Regs::VID_PAL_0, 2, colour0))
            {
                int ind = colour0 & 1;
                m_pImageWidget->m_colours[ind    ] = 0xff000000 | 0x000000;
                m_pImageWidget->m_colours[ind ^ 1] = 0xff000000 | 0xffffff;
            }
        }
    }

    const Memory* pMemOrig = m_pTargetModel->GetMemory(MemorySlot::kGraphicsInspector);
    if (!pMemOrig)
        return;

    EffectiveData data;
    GetEffectiveData(data);

    Mode mode = data.mode;
    int width = data.width;
    int height = data.height;

    // Uncompress
    int required = data.requiredSize;

    // Ensure we have the right size memory
    if ((int)pMemOrig->GetSize() < required)
        return;

    int bitmapSize = width * 16 * height;
    uint8_t* pDestPixels = m_pImageWidget->AllocBitmap(bitmapSize);

    if (mode == k4Bitplane)
    {
        for (int y = 0; y < height; ++y)
        {
            const uint8_t* pChunk = pMemOrig->GetData() + y * data.bytesPerLine;
            for (int x = 0; x < width; ++x)
            {
                int32_t pSrc[4];    // top 16 bits never used
                pSrc[3] = (pChunk[0] << 8) | pChunk[1];
                pSrc[2] = (pChunk[2] << 8) | pChunk[3];
                pSrc[1] = (pChunk[4] << 8) | pChunk[5];
                pSrc[0] = (pChunk[6] << 8) | pChunk[7];
                for (int pix = 15; pix >= 0; --pix)
                {
                    uint8_t val;
                    val  = (pSrc[0] & 1); val <<= 1;
                    val |= (pSrc[1] & 1); val <<= 1;
                    val |= (pSrc[2] & 1); val <<= 1;
                    val |= (pSrc[3] & 1);

                    pDestPixels[pix] = val;
                    pSrc[0] >>= 1;
                    pSrc[1] >>= 1;
                    pSrc[2] >>= 1;
                    pSrc[3] >>= 1;
                }
                pChunk += 8;
                pDestPixels += 16;
            }
        }
    }
    else if (mode == k3Bitplane)
    {
        for (int y = 0; y < height; ++y)
        {
            const uint8_t* pChunk = pMemOrig->GetData() + y * data.bytesPerLine;
            for (int x = 0; x < width; ++x)
            {
                int32_t pSrc[3];    // top 16 bits never used
                pSrc[2] = (pChunk[0] << 8) | pChunk[1];
                pSrc[1] = (pChunk[2] << 8) | pChunk[3];
                pSrc[0] = (pChunk[4] << 8) | pChunk[5];
                for (int pix = 15; pix >= 0; --pix)
                {
                    uint8_t val;
                    val  = (pSrc[0] & 1); val <<= 1;
                    val |= (pSrc[1] & 1); val <<= 1;
                    val |= (pSrc[2] & 1);

                    pDestPixels[pix] = val;
                    pSrc[0] >>= 1;
                    pSrc[1] >>= 1;
                    pSrc[2] >>= 1;
                }
                pChunk += 6;
                pDestPixels += 16;
            }
        }
    }
    else if (mode == k2Bitplane)
    {
        for (int y = 0; y < height; ++y)
        {
            const uint8_t* pChunk = pMemOrig->GetData() + y * data.bytesPerLine;
            for (int x = 0; x < width; ++x)
            {
                int32_t pSrc[2];
                pSrc[1] = (pChunk[0] << 8) | pChunk[1];
                pSrc[0] = (pChunk[2] << 8) | pChunk[3];
                for (int pix = 15; pix >= 0; --pix)
                {
                    uint8_t val;
                    val  = (pSrc[0] & 1); val <<= 1;
                    val |= (pSrc[1] & 1);
                    pDestPixels[pix] = val;
                    pSrc[0] >>= 1;
                    pSrc[1] >>= 1;
                }
                pChunk += 4;
                pDestPixels += 16;
            }
        }
    }
    else if (mode == k1Bitplane)
    {
        for (int y = 0; y < height; ++y)
        {
            const uint8_t* pChunk = pMemOrig->GetData() + y * data.bytesPerLine;
            for (int x = 0; x < width; ++x)
            {
                int32_t pSrc[1];
                pSrc[0] = (pChunk[0] << 8) | pChunk[1];
                for (int pix = 15; pix >= 0; --pix)
                {
                    uint8_t val;
                    val  = (pSrc[0] & 1);
                    pDestPixels[pix] = val;
                    pSrc[0] >>= 1;
                }
                pChunk += 2;
                pDestPixels += 16;
            }
        }
    }
    m_pImageWidget->setPixmap(width, height);

    // Update annotations
    //EffectiveData data;
    //GetEffectiveData(data);
    UpdateAnnotations();
}

void GraphicsInspectorWidget::UpdateUIElements()
{
    m_pWidthSpinBox->setValue(m_width);
    m_pHeightSpinBox->setValue(m_height);
    m_pPaddingSpinBox->setValue(m_padding);
    m_pModeComboBox->setCurrentIndex(m_mode);

    m_pWidthSpinBox->setEnabled(!m_pLockFormatToVideoCheckBox->isChecked());
    m_pHeightSpinBox->setEnabled(!m_pLockFormatToVideoCheckBox->isChecked());
    m_pPaddingSpinBox->setEnabled(!m_pLockFormatToVideoCheckBox->isChecked());
    m_pModeComboBox->setEnabled(!m_pLockFormatToVideoCheckBox->isChecked());

    m_pPaletteAddressLineEdit->setVisible(m_paletteMode == kUserMemory);

    m_pOverlayDarkenAction->setChecked(m_pImageWidget->GetDarken());
    m_pOverlayGridAction->setChecked(m_pImageWidget->GetGrid());
    m_pOverlayZoomAction->setChecked(m_pImageWidget->GetZoom());
    m_pOverlayRegistersAction->setChecked(m_annotateRegisters);
    DisplayAddress();
}

GraphicsInspectorWidget::Mode GraphicsInspectorWidget::GetEffectiveMode() const
{
    if (!m_pLockFormatToVideoCheckBox->isChecked())
        return m_mode;

    const Memory* pMem = m_pTargetModel->GetMemory(MemorySlot::kGraphicsInspectorVideoRegs);
    if (!pMem)
        return Mode::k4Bitplane;

    uint8_t val = 0;
    if (!pMem->ReadAddressByte(Regs::VID_SHIFTER_RES, val))
        return Mode::k1Bitplane;

    Regs::RESOLUTION modeReg = Regs::GetField_VID_SHIFTER_RES_RES(val);
    if (modeReg == Regs::RESOLUTION::LOW)
        return Mode::k4Bitplane;
    else if (modeReg == Regs::RESOLUTION::MEDIUM)
        return Mode::k2Bitplane;

    return Mode::k1Bitplane;
}

int GraphicsInspectorWidget::GetEffectiveWidth() const
{
    if (!m_pLockFormatToVideoCheckBox->isChecked())
        return m_width;

    const Memory* pMem = m_pTargetModel->GetMemory(MemorySlot::kGraphicsInspectorVideoRegs);
    if (!pMem)
        return Mode::k4Bitplane;

    // Handle ST scroll
    int width = 0;
    uint8_t tmpReg = 0;
    if (!IsMachineST(m_pTargetModel->GetMachineType()))
    {
        uint8_t modeReg;
        pMem->ReadAddressByte(Regs::VID_HORIZ_SCROLL_STE, tmpReg);
        modeReg = Regs::GetField_VID_HORIZ_SCROLL_STE_PIXELS(tmpReg);
        if (modeReg != 0)
            width = 1;  // extra read for scroll
    }

    pMem->ReadAddressByte(Regs::VID_SHIFTER_RES, tmpReg);
    Regs::RESOLUTION modeReg = Regs::GetField_VID_SHIFTER_RES_RES(tmpReg);
    if (modeReg == Regs::RESOLUTION::LOW)
        return width + 20;
    else if (modeReg == Regs::RESOLUTION::MEDIUM)
        return width + 40;
    return width + 40;
}

int GraphicsInspectorWidget::GetEffectiveHeight() const
{
    if (!m_pLockFormatToVideoCheckBox->isChecked())
        return m_height;

    const Memory* pMem = m_pTargetModel->GetMemory(MemorySlot::kGraphicsInspectorVideoRegs);
    if (!pMem)
        return Mode::k4Bitplane;
    uint8_t tmpReg;
    pMem->ReadAddressByte(Regs::VID_SHIFTER_RES, tmpReg);
    Regs::RESOLUTION modeReg = Regs::GetField_VID_SHIFTER_RES_RES(tmpReg);
    if (modeReg == Regs::RESOLUTION::LOW)
        return 200;
    else if (modeReg == Regs::RESOLUTION::MEDIUM)
        return 200;
    return 400;
}

int GraphicsInspectorWidget::GetEffectivePadding() const
{
    if (!m_pLockFormatToVideoCheckBox->isChecked())
        return m_padding;

    return 0;       // TODO handle STE padding
}

void GraphicsInspectorWidget::GetEffectiveData(GraphicsInspectorWidget::EffectiveData &data) const
{
    data.mode = GetEffectiveMode();
    data.width = GetEffectiveWidth();
    data.height = GetEffectiveHeight();
    data.bytesPerLine = data.width * BytesPerMode(data.mode) + GetEffectivePadding();
    data.requiredSize = data.bytesPerLine * data.height;
}

int32_t GraphicsInspectorWidget::BytesPerMode(GraphicsInspectorWidget::Mode mode)
{
    switch (mode)
    {
    case k4Bitplane: return 8;
    case k3Bitplane: return 6;
    case k2Bitplane: return 4;
    case k1Bitplane: return 2;
    }
    return 0;
}

void GraphicsInspectorWidget::UpdateAnnotations()
{
    EffectiveData data;
    GetEffectiveData(data);

    QVector<NonAntiAliasImage::Annotation> annots;
    if (m_annotateRegisters)
    {
        for (int i = 0; i < 8; ++i)
        {
            NonAntiAliasImage::Annotation annot;
            uint32_t regAddr = m_pTargetModel->GetRegs().GetAReg(i);
            if (CreateAnnotation(annot, regAddr, data, Registers::s_names[Registers::A0 + i]))
                annots.append(annot);

            uint32_t dregAddr = m_pTargetModel->GetRegs().GetDReg(i);
            if (CreateAnnotation(annot, dregAddr, data, Registers::s_names[Registers::D0 + i]))
                annots.append(annot);
        }
    }

#if 0
    uint32_t symAddr = m_bitmapAddress + data.requiredSize;
    Symbol sym;
    while (symAddr >= m_bitmapAddress)
    {
        if (!m_pTargetModel->GetSymbolTable().FindLowerOrEqual(symAddr, false, sym))
            break;

        NonAntiAliasImage::Annotation annot;
        if (CreateAnnotation(annot, sym.address, data, sym.name.c_str()))
            annots.append(annot);
        symAddr = sym.address - 1;
    }
#endif
    m_pImageWidget->SetAnnotations(annots);
}

bool GraphicsInspectorWidget::CreateAnnotation(NonAntiAliasImage::Annotation &annot, uint32_t address,
                                               const EffectiveData& data,
                                               const char* label)
{
    if (address < m_bitmapAddress || address >= m_bitmapAddress + data.requiredSize)
        return false;

    uint32_t offset = address - m_bitmapAddress;
    uint32_t y = offset / data.bytesPerLine;
    uint32_t x_offset = offset - (y * data.bytesPerLine);

    uint32_t chunk = x_offset / BytesPerMode(m_mode);
    annot.x = chunk * 16;
    annot.y = y;
    annot.text = label;
    return true;
}

void GraphicsInspectorWidget::KeyboardContextMenu()
{
    ContextMenu(mapToGlobal(QPoint(this->width() / 2, this->height() / 2)));
}

void GraphicsInspectorWidget::ContextMenu(QPoint pos)
{
    if (!m_pTargetModel->IsConnected())
        return;

    // Right click menus are instantiated on demand, so we can
    // dynamically add to them
    QMenu menu(this);

    // Add the default actions
    menu.addAction(m_pSaveImageAction);
    menu.addMenu(m_pOverlayMenu);

    m_showAddressMenus[0].setAddress(m_pSession, m_bitmapAddress);
    m_showAddressMenus[0].setTitle(Format::to_hex32(m_bitmapAddress) + QString(" (Bitmap address)"));
    menu.addMenu(m_showAddressMenus[0].m_pMenu);

    if (m_mouseInfo.isValid && m_addressUnderMouse != ~0U)
    {
        m_showAddressMenus[1].setAddress(m_pSession, m_addressUnderMouse);
        m_showAddressMenus[1].setTitle(Format::to_hex32(m_addressUnderMouse) + QString(" (Mouse Cursor address)"));
        menu.addMenu(m_showAddressMenus[1].m_pMenu);
    }

    menu.exec(pos);
}

