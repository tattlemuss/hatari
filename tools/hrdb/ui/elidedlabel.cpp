#include "elidedlabel.h"

#include <QPainter>
#include <QSizePolicy>

ElidedLabel::ElidedLabel(const QString &text, QWidget *parent)
    : QFrame(parent)
    , content(text)
{
    // This workaround forces the label to appear by default...

    setMinimumHeight(1);
    // Set the preferred height here
    setSizePolicy(QSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred,
                                 QSizePolicy::Label));
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
    setMinimumHeight(fontMetrics.lineSpacing());

    int lineSpacing = fontMetrics.lineSpacing();
    QString elidedLastLine = fontMetrics.elidedText(content, Qt::ElideRight, width());

    // Vertical centre
    int y = (height() - lineSpacing) / 2;
    painter.drawText(QPoint(0, y + fontMetrics.ascent()), elidedLastLine);
}

