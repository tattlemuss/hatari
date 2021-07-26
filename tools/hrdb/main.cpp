#include "ui/mainwindow.h"

#include <QApplication>
#include <QSettings>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("hrdb");
    QCoreApplication::setApplicationName("hrdb");
    QSettings::setDefaultFormat(QSettings::Format::IniFormat);

    QApplication a(argc, argv);

    MainWindow w;
    w.show();
    return a.exec();
}
