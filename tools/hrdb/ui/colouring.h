#ifndef COLOURING_H
#define COLOURING_H

#include <QWidget>
#include <QGuiApplication>

namespace Colouring
{
    // Update the widget's palette to show error/success
    inline void SetErrorState(QWidget* pWidget, bool isSuccess)
    {
        QColor highCol = isSuccess ? QGuiApplication::palette().highlight().color() : Qt::red;
        QColor nohighCol = isSuccess ? QGuiApplication::palette().button().color() : Qt::red;
        QPalette pal = pWidget->palette();

        pal.setColor(QPalette::Highlight, highCol);
        pal.setColor(QPalette::Button, nohighCol);
        pWidget->setPalette(pal);
    }
}
#endif // COLOURING_H
