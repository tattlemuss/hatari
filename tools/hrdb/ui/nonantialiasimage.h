#ifndef NONANTIALIASIMAGE_H
#define NONANTIALIASIMAGE_H

#include <QWidget>

class Session;
class QContextMenuEvent;

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

    struct MouseInfo
    {
        bool isValid;
        int x;
        int y;
        int pixelValue;
    };

    const MouseInfo& GetMouseInfo() { return m_pixelInfo; }
signals:
    void MouseInfoChanged();

protected:
    virtual void paintEvent(QPaintEvent*) override;
    virtual void mouseMoveEvent(QMouseEvent *event) override;
    virtual void leaveEvent(QEvent *event) override;
    virtual void keyPressEvent(QKeyEvent *event) override;
    virtual void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void KeyboardContextMenu();
    void ContextMenu(QPoint pos);
    void settingsChanged();
    void saveImageClicked();

    void UpdateString();

    Session*        m_pSession;
    QPixmap         m_pixmap;
    QPointF         m_mousePos;
    QRect           m_renderRect;       // rectangle image was last drawn into

    // Underlying bitmap data
    QImage          m_img;

    uint8_t*        m_pBitmap;
    int             m_bitmapSize;

    MouseInfo       m_pixelInfo;
    bool            m_bRunningMask;

    // Context menu
    QAction*        m_pSaveImageAction;
};


#endif // NONANTIALIASIMAGE_H
