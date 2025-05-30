#ifndef QTVERSIONWRAPPER_H
#define QTVERSIONWRAPPER_H

#include <QVersionNumber>
// Macros/functions to provide cleaner build compatibility between Qt5 and Qt6.
#if QT_VERSION_MAJOR == 5
#define QTEVENT_GET_LOCAL_POS(evptr)   (evptr)->localPos()

#endif
#if QT_VERSION_MAJOR == 6
#define QTEVENT_GET_LOCAL_POS(evptr)   (evptr)->position()


#endif //version 5


#endif // QTVERSIONWRAPPER_H
