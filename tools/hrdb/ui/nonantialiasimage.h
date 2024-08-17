#ifndef NONANTIALIASIMAGE_H
#define NONANTIALIASIMAGE_H

#include <QWidget>

class Session;

// Taken from https://forum.qt.io/topic/94996/qlabel-and-image-antialiasing/5
class NonAntiAliasImage : public QWidget
{
    Q_OBJECT
    Q_DISABLE_COPY(NonAntiAliasImage)
public:
    NonAntiAliasImage(QWidget* parent, Session* pSession);
    virtual ~NonAntiAliasImage() override;

    enum Mode
    {
        kIndexed,           // requires U8 data
        kTruColor           // requires RGB32 data (BB GG RR xx in memory)
    };
    // Set dimensions and refresh data from allocated bitmap area
    void SetPixmap(Mode mode, int width, int height);

    // Refresh m_img from the stored uint8_t* data/palette
    void RefreshPixmap();

    // Data access/writing

    // Enure that N bytes are allocated for main pixel data storage.
    // - indexed needs 1 byte/pixel
    // - TruColor need 4 bytes/pixel
    uint8_t* AllocPixelData(int size);
    QVector<QRgb>   m_colours;

    void SetRunning(bool runFlag);

    // Access UI image, used for "Save Image".
    const QImage& GetImage() const { return m_img; }

    // Describes what's currently under the mouse pointer
    struct MouseInfo
    {
        bool isValid;       // is there a pixel here?
        int x;
        int y;
        QString pixelValue;     // currently: palette value, or "" if invalid
    };

    const MouseInfo& GetMouseInfo() { return m_pixelInfo; }

    struct Annotation
    {
        int x;
        int y;
        QString text;
    };

    void SetAnnotations(const QVector<Annotation>& annots);

    bool GetGrid() const { return m_enableGrid; }
    void SetGrid(bool enable);

    bool GetDarken() const { return m_darken; }
    void SetDarken(bool enable);

    bool GetZoom() const { return m_enableZoom; }
    void SetZoom(bool enable);

signals:
    void MouseInfoChanged();

protected:
    virtual void paintEvent(QPaintEvent*) override;
    virtual void mouseMoveEvent(QMouseEvent *event) override;
    virtual void leaveEvent(QEvent *event) override;

private:
    static const int kZoomRatio = 20;
    static const int kZoomPixelBorder = 4;  // number of pixels around the cursor to grab in each direction

    void KeyboardContextMenu();
    void settingsChanged();
    void UpdateMouseInfo();
    void DrawZoom(QPainter& painter) const;

    QPoint ScreenPointFromBitmapPoint(const QPoint &bitmapPoint, const QRect &rect) const;
    QPoint BitmapPointFromScreenPoint(const QPoint &bitmapPoint, const QRect &rect) const;

    Session*        m_pSession;         // Used for settings change
    Mode            m_mode;
    int             m_width;
    int             m_height;
    QPixmap         m_pixmap;
    QPointF         m_mousePos;
    QRect           m_renderRect;       // rectangle image was last drawn into

    // Underlying bitmap data
    QImage          m_img;              // Qt wrapper for m_pBitmap

    // The raw data supplied by the client
    uint8_t*        m_pPixelData;
    int             m_bitmapSize;

    MouseInfo       m_pixelInfo;
    bool            m_bRunningMask;

    // Overlays/helpers\s
    bool            m_darken;
    bool            m_enableGrid;
    bool            m_enableZoom;
    QVector<Annotation> m_annotations;
};


#endif // NONANTIALIASIMAGE_H
