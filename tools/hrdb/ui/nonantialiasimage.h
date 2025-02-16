#ifndef NONANTIALIASIMAGE_H
#define NONANTIALIASIMAGE_H

#include <QWidget>
#include "memorybitmap.h"

class Session;

// Taken from https://forum.qt.io/topic/94996/qlabel-and-image-antialiasing/5
class NonAntiAliasImage : public QWidget
{
    Q_OBJECT
    Q_DISABLE_COPY(NonAntiAliasImage)
public:
    NonAntiAliasImage(QWidget* parent, Session* pSession);
    virtual ~NonAntiAliasImage() override;

    void SetRunning(bool runFlag);

    MemoryBitmap m_bitmap;

    const MemoryBitmap::PixelInfo& GetMouseInfo() { return m_pixelInfo; }

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

    QPointF         m_mousePos;
    QRect           m_renderRect;       // rectangle image was last drawn into

    MemoryBitmap::PixelInfo m_pixelInfo;
    bool            m_bRunningMask;

    // Overlays/helpers\s
    bool            m_darken;
    bool            m_enableGrid;
    bool            m_enableZoom;
    QVector<Annotation> m_annotations;
};


#endif // NONANTIALIASIMAGE_H
