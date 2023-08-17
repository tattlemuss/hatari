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

    void setPixmap(int width, int height);
    uint8_t* AllocBitmap(int size);
    void SetRunning(bool runFlag);
    const QImage& GetImage() const { return m_img; }
    QVector<QRgb>   m_colours;

    // Describes what's currently under the mouse pointer
    struct MouseInfo
    {
        bool isValid;       // is there a pixel here?
        int x;
        int y;
        int pixelValue;     // currently: palette value, or -1 if invalid
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

signals:
    void MouseInfoChanged();

protected:
    virtual void paintEvent(QPaintEvent*) override;
    virtual void mouseMoveEvent(QMouseEvent *event) override;
    virtual void leaveEvent(QEvent *event) override;

private:
    void KeyboardContextMenu();
    void settingsChanged();
    void UpdateMouseInfo();
    QPoint ScreenPointFromBitmapPoint(const QPoint &bitmapPoint, const QRect &rect) const;
    QPoint BitmapPointFromScreenPoint(const QPoint &bitmapPoint, const QRect &rect) const;

    Session*        m_pSession;         // Used for settings change
    QPixmap         m_pixmap;
    QPointF         m_mousePos;
    QRect           m_renderRect;       // rectangle image was last drawn into

    // Underlying bitmap data
    QImage          m_img;

    uint8_t*        m_pBitmap;
    int             m_bitmapSize;

    MouseInfo       m_pixelInfo;
    bool            m_bRunningMask;

    // Overlays/helpers\s
    bool            m_darken;
    bool            m_enableGrid;
    QVector<Annotation> m_annotations;
};


#endif // NONANTIALIASIMAGE_H
