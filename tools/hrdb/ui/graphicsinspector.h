#ifndef GRAPHICSINSPECTOR_H
#define GRAPHICSINSPECTOR_H

#include <QDockWidget>
#include <QObject>

// Forward declarations
#include "../models/session.h"  // for WindowType
#include "../hardware/regs_st.h"
#include "nonantialiasimage.h"  // for MouseInfo
#include "showaddressactions.h"

class QLabel;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QToolButton;
class ElidedLabel;

class TargetModel;
class SymbolTableModel;
class Dispatcher;

class GraphicsInspectorWidget : public QDockWidget
{
    Q_OBJECT
public:
    GraphicsInspectorWidget(QWidget *parent,
                            Session* pSession);
    virtual ~GraphicsInspectorWidget() override;

    // Grab focus and point to the main widget
    void keyFocus();
    void loadSettings();
    void saveSettings();

private:
    void connectChanged();
    void startStopChanged();
    void startStopDelayedChanged();
    void memoryChanged(int memorySlot, uint64_t commandId);
    void otherMemoryChanged(uint32_t address, uint32_t size);
    void symbolTableChanged(uint64_t requestId);

    void runningRefreshTimer();
    void bitmapAddressChanged();
    void paletteAddressChanged();
    void lockAddressToVideoChanged();
    void overlayDarkenChanged();
    void overlayGridChanged();
    void overlayZoomChanged();
    void overlayRegistersChanged();

private slots:
    // These are genuine slots
    void modeChangedSlot(int index);
    void paletteChangedSlot(int index);
    void gotoClickedSlot();
    void lockClickedSlot();
    void StrideChangedSlot(int width);
    void leftStrideClicked();
    void rightStrideClicked();
    void heightChangedSlot(int height);
    void saveImageClicked();

private:
    void updateInfoLine();
protected:
    virtual void keyPressEvent(QKeyEvent *ev) override;
    virtual void contextMenuEvent(QContextMenuEvent *event) override;

private:
    enum Mode
    {
        kFormat4Bitplane,
        kFormat3Bitplane,
        kFormat2Bitplane,
        kFormat1Bitplane,
        kFormatRegisters,
        kFormat1BPP,
        kFormatTruColor       // Falcon 16bpp
    };

    enum Palette
    {
        kGreyscale,
        kContrast1,
        kBitplane0,
        kBitplane1,
        kBitplane2,
        kBitplane3,
        kRegisters,
        kUserMemory
    };

    void RequestBitmapAddress(Session::WindowType type, int windowIndex, int memorySpace, uint32_t address);

    // Looks at dirty requests, and issues them in the correct orders
    void UpdateMemoryRequests();

    // Turn boxes on/off depending on mode, palette etc
    void UpdateUIElements();

    bool SetBitmapAddressFromVideoRegs();

    // Update the text boxes with the active bitmap/palette addresses
    void DisplayAddress();

    // Copy format from video regs if required
    void UpdateFormatFromUI();

    // Finally update palette+bitmap for display
    void UpdateImage();

    struct EffectiveData
    {
        int height;
        Mode mode;
        int bytesPerLine;
        int requiredSize;
        int pixels;
    };

    // Get the effective data by checking the "lock to" flags and
    // using them if necessary.
    GraphicsInspectorWidget::Mode GetEffectiveMode() const;
    int GetEffectiveStride() const;
    int GetEffectiveHeight() const;
    void GetEffectiveData(EffectiveData& data) const;

    // Calc the size of a 16-pixel chunk depending on mode
    static int32_t BytesPerChunk(Mode mode);

    void UpdateAnnotations();

    bool CreateAnnotation(NonAntiAliasImage::Annotation &annot, uint32_t address,
                          const EffectiveData &data, const char* label);

    void KeyboardContextMenu();
    void ContextMenu(QPoint pos);

    QLineEdit*          m_pBitmapAddressLineEdit;
    QLineEdit*          m_pPaletteAddressLineEdit;
    QComboBox*          m_pModeComboBox;
    QSpinBox*           m_pStrideSpinBox;
    QToolButton*        m_pLeftStrideButton;
    QToolButton*        m_pRightStrideButton;
    QSpinBox*           m_pHeightSpinBox;
    QCheckBox*          m_pLockAddressToVideoCheckBox;
    QComboBox*          m_pPaletteComboBox;
    ElidedLabel*        m_pWidthInfoLabel;
    ElidedLabel*        m_pMouseInfoLabel;

    NonAntiAliasImage*  m_pImageWidget;

    Session*            m_pSession;
    TargetModel*        m_pTargetModel;
    Dispatcher*         m_pDispatcher;

    SymbolTableModel*   m_pSymbolTableModel;

    // logical UI state shadowing widgets
    Mode            m_mode;
    uint32_t        m_bitmapAddress;
    int             m_userBytesPerLine;
    int             m_userHeight;

    Palette         m_paletteMode;
    uint32_t        m_paletteAddress;

    // Cached information when video register fetch happens.
    // Used to control palette when in Mono mode.
    Regs::RESOLUTION    m_cachedResolution;

    // Stores state of "memory wanted" vs "memory request in flight"
    struct Request
    {
        bool isDirty;       // We've put in a request
        uint64_t requestId; // ID of active mem request, or 0

        Request()
        {
            Clear();
        }
        void Clear()
        {
            isDirty = false; requestId = 0;
        }

        void Dirty()
        {
            isDirty = true; requestId = 0;
        }
    };

    Request                         m_requestRegs;
    Request                         m_requestPalette;
    Request                         m_requestBitmap;

    // Mouseover data
    MemoryBitmap::PixelInfo         m_mouseInfo;            // data from MouseOver in bitmap
    uint32_t                        m_addressUnderMouse;    // ~0U for "invalid"

    // Options
    bool                            m_annotateRegisters;

    // Context menu actions
    QAction*                        m_pSaveImageAction;
    QMenu*                          m_pOverlayMenu;
    QAction*                        m_pOverlayDarkenAction;
    QAction*                        m_pOverlayGridAction;
    QAction*                        m_pOverlayZoomAction;
    QAction*                        m_pOverlayRegistersAction;

    // "Show memory for $x" top-level menus:
    // [0] Show Base Address
    // [1] Show Memory under mouse
    ShowAddressMenu                 m_showAddressMenus[2];
};

#endif // GRAPHICSINSPECTOR_H
