#include "nonantialiasimage.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QMenu>
#include "../models/session.h"
#include "../models/targetmodel.h"

NonAntiAliasImage::NonAntiAliasImage(QWidget *parent, Session* pSession)
    : QWidget(parent),
      m_pSession(pSession),
      m_pBitmap(nullptr),
      m_bitmapSize(0),
      m_bRunningMask(false),
      m_darken(false),
      m_enableGrid(false),
      m_enableZoom(false)
{
    m_renderRect = rect();
    setMouseTracking(true);
    setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    m_pixelInfo.isValid = false;
    m_pixelInfo.x = 0;
    m_pixelInfo.y = 0;
    m_pixelInfo.pixelValue = 0;
    setCursor(Qt::CrossCursor);
    connect(m_pSession,         &Session::settingsChanged, this, &NonAntiAliasImage::settingsChanged);
}

void NonAntiAliasImage::setPixmap(int width, int height)
{
    // Regenerate a new shape
    m_img = QImage(m_pBitmap, width, height, width, QImage::Format_Indexed8);
    m_img.setColorTable(m_colours);
    QPixmap pm = QPixmap::fromImage(m_img);
    m_pixmap = pm;
    UpdateMouseInfo();
    emit MouseInfoChanged();
    update();
}

NonAntiAliasImage::~NonAntiAliasImage()
{
    delete [] m_pBitmap;
}

uint8_t* NonAntiAliasImage::AllocBitmap(int size)
{
    if (size == m_bitmapSize)
        return m_pBitmap;

    delete [] m_pBitmap;
    m_pBitmap = new uint8_t[size];
    m_bitmapSize = size;
    return m_pBitmap;
}

void NonAntiAliasImage::SetRunning(bool runFlag)
{
    m_bRunningMask = runFlag;
    update();
}

void NonAntiAliasImage::SetAnnotations(const QVector<NonAntiAliasImage::Annotation> &annots)
{
    m_annotations = annots;
    update();
}

void NonAntiAliasImage::SetGrid(bool enable)
{
    m_enableGrid = enable;
    update();
}

void NonAntiAliasImage::SetDarken(bool enable)
{
    m_darken = enable;
    update();
}

void NonAntiAliasImage::SetZoom(bool enable)
{
    m_enableZoom = enable;
    update();
}

void NonAntiAliasImage::paintEvent(QPaintEvent* ev)
{
    QPainter painter(this);
    QRect r = rect();
    const int border = 6;
    // Reduce so that the border doesn't overlap the image
    r.adjust(border, border, -border, -border);

    QPalette pal = this->palette();
    painter.setFont(m_pSession->GetSettings().m_font);
    if (m_pSession->m_pTargetModel->IsConnected())
    {
        if (m_pixmap.width() != 0 && m_pixmap.height() != 0)
        {
            // Draw the pixmap with square pixels
            if (m_pSession->GetSettings().m_bSquarePixels)
            {
                float texelsToPixelsX = r.width() / static_cast<float>(m_pixmap.width());
                float texelsToPixelsY = r.height() / static_cast<float>(m_pixmap.height());
                float minRatio = std::min(texelsToPixelsX, texelsToPixelsY);

                QRect fixedR(r.x(), r.y(), minRatio * m_pixmap.width(), minRatio * m_pixmap.height());
                painter.setRenderHint(QPainter::Antialiasing, false);
                style()->drawItemPixmap(&painter, fixedR, Qt::AlignCenter, m_pixmap.scaled(fixedR.size()));
                m_renderRect = fixedR;
            }
            else {
                style()->drawItemPixmap(&painter, r, Qt::AlignCenter, m_pixmap.scaled(r.size()));
                m_renderRect = r;
            }
        }

        bool runningMask = m_bRunningMask && !m_pSession->GetSettings().m_liveRefresh;

        bool darkenMask = runningMask || m_darken;

        // Darken the area if we don't have live refresh
        if (darkenMask)
        {
            painter.setBrush(QBrush(QColor(0, 0, 0, 128)));
            painter.drawRect(r);
        }

        if (runningMask)
        {
            painter.setPen(Qt::magenta);
            painter.setBrush(Qt::NoBrush);
            painter.drawText(r, Qt::AlignCenter, "Running...");
        }

        painter.setPen(Qt::magenta);
        painter.setBrush(Qt::NoBrush);
        if (m_pixmap.width() && m_pixmap.height())
        {
            for (const Annotation& annot : m_annotations)
            {
                QPoint pt = ScreenPointFromBitmapPoint(QPoint(annot.x, annot.y), m_renderRect);
                painter.drawLine(pt, pt + QPoint(5, 0));
                painter.drawLine(pt, pt + QPoint(0, 5));
                painter.drawText(pt + QPoint(7,5), annot.text);
            }
        }

        if (m_enableGrid)
        {
            for (int x = 0; x < m_pixmap.width(); x += 16)
            {
                QPoint pt0 = ScreenPointFromBitmapPoint(QPoint(x, 0), m_renderRect);
                QPoint pt1 = ScreenPointFromBitmapPoint(QPoint(x, m_pixmap.height()), m_renderRect);
                painter.drawLine(pt0, pt1);
            }
            for (int y = 0; y < m_pixmap.height(); y += 16)
            {
                QPoint pt0 = ScreenPointFromBitmapPoint(QPoint(0, y), m_renderRect);
                QPoint pt1 = ScreenPointFromBitmapPoint(QPoint(m_pixmap.width(), y), m_renderRect);
                painter.drawLine(pt0, pt1);
            }
        }

        // Zoom
        if (m_enableZoom && m_pixelInfo.isValid)
            DrawZoom(painter);
    }
    else {
        painter.drawText(r, Qt::AlignCenter, "Not connected.");
    }

    painter.setPen(QPen(pal.dark(), hasFocus() ? 6 : 2));
    painter.drawRect(rect());
    QWidget::paintEvent(ev);
}

void NonAntiAliasImage::mouseMoveEvent(QMouseEvent *event)
{
    m_mousePos = event->localPos();
    UpdateMouseInfo();
    emit MouseInfoChanged();
    update();// re-render zoom
    QWidget::mouseMoveEvent(event);
}

void NonAntiAliasImage::leaveEvent(QEvent *event)
{
    m_mousePos = QPoint(-1, -1);
    UpdateMouseInfo();
    emit MouseInfoChanged();
    update();// re-render zoom
    QWidget::leaveEvent(event);
}

void NonAntiAliasImage::settingsChanged()
{
    // Force redraw in case square pixels changed
    UpdateMouseInfo();
    emit MouseInfoChanged();
    update();
}

void NonAntiAliasImage::UpdateMouseInfo()
{
    m_pixelInfo.isValid = false;
    if (m_pixmap.width() == 0)
        return;

    QPoint mpos(static_cast<int>(m_mousePos.x()), static_cast<int>(m_mousePos.y()));
    if (m_renderRect.contains(mpos))
    {
        // Calc bitmap pos from screen pos
        QPoint bmPoint = BitmapPointFromScreenPoint(mpos, m_renderRect);
        int x = bmPoint.x();
        int y = bmPoint.y();
        m_pixelInfo.x = x;
        m_pixelInfo.y = y;
        m_pixelInfo.pixelValue = -1;

        if (x >= 0 &&
            x < m_pixmap.width() &&
            y >= 0 &&
            y < m_pixmap.height() &&
            m_pBitmap)
        {
            m_pixelInfo.pixelValue = m_pBitmap[y * m_pixmap.width() + x];
        }
        m_pixelInfo.isValid = true;
    }
}

void NonAntiAliasImage::DrawZoom(QPainter& painter) const
{
    const int pixelCountX = kZoomPixelBorder * 2 + 1;  // overall rect grab size
    const int pixelCountY = pixelCountX;

    QRect grabRegion(m_pixelInfo.x - kZoomPixelBorder, m_pixelInfo.y - kZoomPixelBorder, pixelCountX, pixelCountY);

    QPoint zoomRenderPos = ScreenPointFromBitmapPoint(QPoint(m_pixelInfo.x, m_pixelInfo.y), m_renderRect);
    zoomRenderPos -= QPoint(pixelCountX * kZoomRatio / 2, pixelCountY * kZoomRatio / 2);
    QPoint zoomRenderPosBase = zoomRenderPos;

    // Clip on sides
    if (grabRegion.x() < 0)
    {
        int adj = grabRegion.x();
        grabRegion.adjust(-adj, 0, 0, 0);     // move in from left
        zoomRenderPos += QPoint(-adj * kZoomRatio, 0);
    }
    if (grabRegion.y() < 0)
    {
        int adj = grabRegion.y();
        grabRegion.adjust(0, -adj, 0, 0);
        zoomRenderPos += QPoint(0, -adj * kZoomRatio);
    }
    // These don't need to adjust the render pos
    if (grabRegion.right() > m_pixmap.width())
        grabRegion.setWidth(m_pixmap.width() - grabRegion.x());
    if (grabRegion.bottom() > m_pixmap.height())
        grabRegion.setHeight(m_pixmap.height() - grabRegion.y());

    QPixmap zoomRec = m_pixmap.copy(grabRegion);
    zoomRec = zoomRec.scaled(grabRegion.size() * kZoomRatio);

    style()->drawItemPixmap(&painter, QRect(zoomRenderPos, QSize(zoomRec.size())), Qt::AlignCenter,
                            zoomRec);

    // Highlight the cursor pixel
    painter.drawRect(zoomRenderPosBase.x() + kZoomPixelBorder * kZoomRatio,
                     zoomRenderPosBase.y() + kZoomPixelBorder * kZoomRatio, kZoomRatio, kZoomRatio);

}

QPoint NonAntiAliasImage::ScreenPointFromBitmapPoint(const QPoint &bitmapPoint, const QRect &rect) const
{
    // Work out position as a proportion of the render rect
    float x = rect.x() + (rect.width() * bitmapPoint.x()) / m_pixmap.width();
    float y = rect.y() + (rect.height() * bitmapPoint.y()) / m_pixmap.height();
    return QPoint(x, y);
}

QPoint NonAntiAliasImage::BitmapPointFromScreenPoint(const QPoint &bitmapPoint, const QRect &rect) const
{
    const QRect& r = rect;
    double x_frac = double(bitmapPoint.x() - r.x()) / r.width();
    double y_frac = double(bitmapPoint.y() - r.y()) / r.height();

    double x_pix = x_frac * m_pixmap.width();
    double y_pix = y_frac * m_pixmap.height();

    int x = static_cast<int>(x_pix);
    int y = static_cast<int>(y_pix);
    return QPoint(x,y);
}
