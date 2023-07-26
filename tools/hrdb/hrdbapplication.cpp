#include "hrdbapplication.h"
#include <QPixmap>
#include <QIcon>

HrdbApplication::HrdbApplication(int &argc, char **argv) :
    QApplication(argc, argv)
{
    QPixmap iconPixmap(":/images/hrdb_icon.png");
    setWindowIcon(QIcon(iconPixmap));
}
