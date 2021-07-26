QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11
CONFIG -= embed_manifest_exe

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    hopper/decode.cpp \
    main.cpp \
    models/breakpoint.cpp \
    models/disassembler.cpp \
    models/exceptionmask.cpp \
    models/memory.cpp \
    models/stringparsers.cpp \
    models/stringsplitter.cpp \
    models/symboltable.cpp \
    models/symboltablemodel.cpp \
    models/targetmodel.cpp \
    transport/dispatcher.cpp \
    ui/addbreakpointdialog.cpp \
    ui/breakpointswidget.cpp \
    ui/disasmwidget.cpp \
    ui/exceptiondialog.cpp \
    ui/graphicsinspector.cpp \
    ui/mainwindow.cpp \
    ui/memoryviewwidget.cpp \
    ui/rundialog.cpp

HEADERS += \
    hopper/buffer.h \
    hopper/decode.h \
    hopper/instruction.h \
    models/breakpoint.h \
    models/disassembler.h \
    models/exceptionmask.h \
    models/memory.h \
    models/registers.h \
    models/stringparsers.h \
    models/stringsplitter.h \
    models/symboltable.h \
    models/symboltablemodel.h \
    models/targetmodel.h \
    transport/dispatcher.h \
    transport/remotecommand.h \
    ui/addbreakpointdialog.h \
    ui/breakpointswidget.h \
    ui/disasmwidget.h \
    ui/exceptiondialog.h \
    ui/graphicsinspector.h \
    ui/mainwindow.h \
    ui/memoryviewwidget.h \
    ui/quicklayout.h \
    ui/rundialog.h

RESOURCES     = hrdb.qrc    

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
