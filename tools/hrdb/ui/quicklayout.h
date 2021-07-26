#ifndef QUICKLAYOUT_H
#define QUICKLAYOUT_H

#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

inline QWidget*    CreateHorizLayout(QWidget* parent, QWidget* pWidgets[])
{
    QHBoxLayout* pLayout = new QHBoxLayout;
    while (*pWidgets != nullptr)
    {
        pLayout->addWidget(*pWidgets);
        ++pWidgets;
    }

    QGroupBox* pBaseWidget = new QGroupBox(parent);
    pBaseWidget->setFlat(true);
    pBaseWidget->setLayout(pLayout);
    return pBaseWidget;
}

inline QWidget*    CreateVertLayout(QWidget* parent, QWidget* pWidgets[])
{
    QVBoxLayout* pLayout = new QVBoxLayout;
    while (*pWidgets != nullptr)
    {
        pLayout->addWidget(*pWidgets);
        ++pWidgets;
    }

    QGroupBox* pBaseWidget = new QGroupBox(parent);
    pBaseWidget->setFlat(true);
    pBaseWidget->setLayout(pLayout);
    return pBaseWidget;
}

#endif // QUICKLAYOUT_H
