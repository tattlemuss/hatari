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
      m_bRunningMask(false)
{
    m_renderRect = rect();
    setMouseTracking(true);
    setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    m_pixelInfo.isValid = false;
    m_pixelInfo.x = 0;
    m_pixelInfo.y = 0;
    m_pixelInfo.pixelValue = 0;
    connect(m_pSession,         &Session::settingsChanged, this, &NonAntiAliasImage::settingsChanged);
}

void NonAntiAliasImage::setPixmap(int width, int height)
{
    // Regenerate a new shape
    m_img = QImage(m_pBitmap, width * 16, height, QImage::Format_Indexed8);
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

void NonAntiAliasImage::paintEvent(QPaintEvent* ev)
{
    QPainter painter(this);
    const QRect& r = rect();

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

                QRect fixedR(0, 0, minRatio * m_pixmap.width(), minRatio * m_pixmap.height());
                painter.setRenderHint(QPainter::Antialiasing, false);
                style()->drawItemPixmap(&painter, fixedR, Qt::AlignCenter, m_pixmap.scaled(fixedR.size()));
                m_renderRect = fixedR;
            }
            else {
                style()->drawItemPixmap(&painter, r, Qt::AlignCenter, m_pixmap.scaled(r.size()));
                m_renderRect = r;
            }
        }

        // Darken the area if we don't have live refresh
        if (m_bRunningMask && !m_pSession->GetSettings().m_liveRefresh)
        {
            painter.setBrush(QBrush(QColor(0, 0, 0, 128)));
            painter.drawRect(r);

            painter.setPen(Qt::magenta);
            painter.setBrush(Qt::NoBrush);
            painter.drawText(r, Qt::AlignCenter, "Running...");
        }
    }
    else {
        painter.drawText(r, Qt::AlignCenter, "Not connected.");
    }

    painter.setPen(QPen(pal.dark(), hasFocus() ? 6 : 2));
    painter.drawRect(r);
    QWidget::paintEvent(ev);
}

void NonAntiAliasImage::mouseMoveEvent(QMouseEvent *event)
{
    m_mousePos = event->localPos();
    UpdateMouseInfo();
    emit MouseInfoChanged();
    QWidget::mouseMoveEvent(event);
}

void NonAntiAliasImage::leaveEvent(QEvent *event)
{
    m_mousePos = QPoint(-1, -1);
    UpdateMouseInfo();
    emit MouseInfoChanged();
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
        const QRect& r = m_renderRect;
        double x_frac = (m_mousePos.x() - r.x()) / r.width();
        double y_frac = (m_mousePos.y() - r.y()) / r.height();

        double x_pix = x_frac * m_pixmap.width();
        double y_pix = y_frac * m_pixmap.height();

        int x = static_cast<int>(x_pix);
        int y = static_cast<int>(y_pix);
        m_pixelInfo.x = x;
        m_pixelInfo.y = y;
        m_pixelInfo.pixelValue = -1;
        if (x < m_pixmap.width() && y < m_pixmap.height() &&
            m_pBitmap)
        {
            m_pixelInfo.pixelValue = m_pBitmap[y * m_pixmap.width() + x];
        }
        m_pixelInfo.isValid = true;
    }
}

