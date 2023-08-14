#include "elidedlabel.h"

#include <QPainter>
#include <QSizePolicy>

ElidedLabel::ElidedLabel(const QString &text, QWidget *parent)
    : QFrame(parent)
    , content(text)
{
    QPainter painter(this);
    QFontMetrics fontMetrics = painter.fontMetrics();
    // Set the preferred height here
    setMinimumHeight(fontMetrics.lineSpacing());

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void ElidedLabel::setText(const QString &newText)
{
    content = newText;
    update();
}

void ElidedLabel::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);

    QPainter painter(this);
    QFontMetrics fontMetrics = painter.fontMetrics();

    int lineSpacing = fontMetrics.lineSpacing();
    QString elidedLastLine = fontMetrics.elidedText(content, Qt::ElideRight, width());

    // Vertical centre
    int y = (height() - lineSpacing) / 2;
    painter.drawText(QPoint(0, y + fontMetrics.ascent()), elidedLastLine);
}

