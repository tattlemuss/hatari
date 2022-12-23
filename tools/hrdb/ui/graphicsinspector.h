#ifndef GRAPHICSINSPECTOR_H
#define GRAPHICSINSPECTOR_H

#include <QDockWidget>
#include <QObject>

// Forward declarations
#include "../models/session.h"
#include "nonantialiasimage.h"

class QLabel;
class QLineEdit;
class QAbstractItemModel;
class QSpinBox;
class QCheckBox;
class QComboBox;

class TargetModel;
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
    void runningRefreshTimer();
    void bitmapAddressChanged();
    void paletteAddressChanged();
    void lockAddressToVideoChanged();
    void lockFormatToVideoChanged();

private slots:
    // These are genuine slots
    void modeChangedSlot(int index);
    void paletteChangedSlot(int index);
    void gotoClickedSlot();
    void lockClickedSlot();
    void widthChangedSlot(int width);
    void heightChangedSlot(int height);
    void paddingChangedSlot(int height);

private:
    void tooltipStringChanged();
protected:
    virtual void keyPressEvent(QKeyEvent *ev) override;

private:
    enum Mode
    {
        k4Bitplane,
        k3Bitplane,
        k2Bitplane,
        k1Bitplane
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

    void RequestBitmapAddress(Session::WindowType type, int windowIndex, uint32_t address);

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
        int width;
        int height;
        Mode mode;
        int bytesPerLine;
        int requiredSize;
    };

    // Get the effective data by checking the "lock to" flags and
    // using them if necessary.
    GraphicsInspectorWidget::Mode GetEffectiveMode() const;
    int GetEffectiveWidth() const;
    int GetEffectiveHeight() const;
    int GetEffectivePadding() const;
    void GetEffectiveData(EffectiveData& data) const;

    static int32_t BytesPerMode(Mode mode);

    QLineEdit*      m_pBitmapAddressLineEdit;
    QLineEdit*      m_pPaletteAddressLineEdit;
    QComboBox*      m_pModeComboBox;
    QSpinBox*       m_pWidthSpinBox;
    QSpinBox*       m_pHeightSpinBox;
    QSpinBox*       m_pPaddingSpinBox;
    QCheckBox*      m_pLockAddressToVideoCheckBox;
    QCheckBox*      m_pLockFormatToVideoCheckBox;
    QComboBox*      m_pPaletteComboBox;
    QLabel*         m_pInfoLabel;

    NonAntiAliasImage*         m_pImageWidget;

    Session*        m_pSession;
    TargetModel*    m_pTargetModel;
    Dispatcher*     m_pDispatcher;

    QAbstractItemModel* m_pSymbolTableModel;

    Mode            m_mode;
    uint32_t        m_bitmapAddress;
    int             m_width;            // in "chunks"
    int             m_height;
    int             m_padding;          // in bytes

    Palette         m_paletteMode;
    uint32_t        m_paletteAddress;

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

    Request         m_requestRegs;
    Request         m_requestPalette;
    Request         m_requestBitmap;
};

#endif // GRAPHICSINSPECTOR_H
