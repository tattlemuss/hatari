#ifndef GRAPHICSINSPECTOR_H
#define GRAPHICSINSPECTOR_H

#include <QDockWidget>
#include <QObject>

// Forward declarations
class QLabel;
class QLineEdit;
class QAbstractItemModel;
class QSpinBox;
class QCheckBox;
class QComboBox;

class Session;
class TargetModel;
class Dispatcher;

// Taken from https://forum.qt.io/topic/94996/qlabel-and-image-antialiasing/5
class NonAntiAliasImage : public QWidget
{
    Q_OBJECT
    Q_DISABLE_COPY(NonAntiAliasImage)
public:
    NonAntiAliasImage(QWidget* parent, Session* pSession);
    virtual ~NonAntiAliasImage() override;

    void setPixmap(int width, int height);
    uint8_t* AllocBitmap(int size);
    void SetRunning(bool runFlag);

    QVector<QRgb>   m_colours;

    const QString& GetString() { return m_infoString; }
signals:
    void StringChanged();

protected:
    virtual void paintEvent(QPaintEvent*) override;
    virtual void mouseMoveEvent(QMouseEvent *event) override;

private slots:
    void settingsChangedSlot();

private:
    void UpdateString();

    Session*        m_pSession;
    QPixmap         m_pixmap;
    QPointF         m_mousePos;

    // Underlying bitmap data
    uint8_t*        m_pBitmap;
    int             m_bitmapSize;

    QString         m_infoString;
    bool            m_bRunningMask;
};

class GraphicsInspectorWidget : public QDockWidget
{
    Q_OBJECT
public:
    GraphicsInspectorWidget(QWidget *parent,
                            Session* pSession);
    ~GraphicsInspectorWidget();

    // Grab focus and point to the main widget
    void keyFocus();
    void loadSettings();
    void saveSettings();
private:
    void connectChangedSlot();
    void startStopChangedSlot();
    void startStopDelayedChangedSlot();
    void memoryChangedSlot(int memorySlot, uint64_t commandId);
    void otherMemoryChangedSlot(uint32_t address, uint32_t size);
    void textEditChangedSlot();
    void lockAddressToVideoChangedSlot();
    void lockFormatToVideoChangedSlot();
    void lockPaletteToVideoChangedSlot();

private slots:
    void modeChangedSlot(int index);
    void paletteChangedSlot(int index);
    void widthChangedSlot(int width);
    void heightChangedSlot(int height);
    void paddingChangedSlot(int height);
    void tooltipStringChangedSlot();
protected:
    virtual void keyPressEvent(QKeyEvent *ev);

private:
    enum Mode
    {
        k4Bitplane,
        k2Bitplane,
        k1Bitplane,
    };

    enum Palette
    {
        kGreyscale,
        kContrast1,
        kBitplane0,
        kBitplane1,
        kBitplane2,
        kBitplane3,
    };

    void UpdateUIElements();
    void RequestMemory();

    bool SetAddressFromVideo();
    void DisplayAddress();

    // Either copy from registers, or use the user settings
    void UpdatePaletteFromSettings();

    // Copy format from video regs if required
    void UpdateFormatFromSettings();

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

    QLineEdit*      m_pAddressLineEdit;
    QComboBox*      m_pModeComboBox;
    QSpinBox*       m_pWidthSpinBox;
    QSpinBox*       m_pHeightSpinBox;
    QSpinBox*       m_pPaddingSpinBox;
    QCheckBox*      m_pLockAddressToVideoCheckBox;
    QCheckBox*      m_pLockFormatToVideoCheckBox;
    QCheckBox*      m_pLockPaletteToVideoCheckBox;
    QComboBox*      m_pPaletteComboBox;
    QLabel*         m_pInfoLabel;

    NonAntiAliasImage*         m_pImageWidget;

    Session*        m_pSession;
    TargetModel*    m_pTargetModel;
    Dispatcher*     m_pDispatcher;

    QAbstractItemModel* m_pSymbolTableModel;

    Mode            m_mode;
    uint32_t        m_address;
    int             m_width;            // in "chunks"
    int             m_height;
    int             m_padding;          // in bytes

    uint64_t        m_requestIdBitmap;
    uint64_t        m_requestIdVideoRegs;
};

#endif // GRAPHICSINSPECTOR_H
