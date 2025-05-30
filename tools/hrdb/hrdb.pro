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
    hardware/hardware_st.cpp \
    hardware/regs_st.cpp \
    hardware/tos.cpp \
    hopper/decode.cpp \
    hopper/instruction.cpp \
    hrdbapplication.cpp \
    main.cpp \
    models/breakpoint.cpp \
    models/disassembler.cpp \
    models/exceptionmask.cpp \
    models/launcher.cpp \
    models/memory.cpp \
    models/profiledata.cpp \
    models/registers.cpp \
    models/session.cpp \
    models/stringparsers.cpp \
    models/stringsplitter.cpp \
    models/symboltable.cpp \
    models/symboltablemodel.cpp \
    models/targetmodel.cpp \
    models/filewatcher.cpp \
    transport/dispatcher.cpp \
    ui/addbreakpointdialog.cpp \
    ui/breakpointswidget.cpp \
    ui/consolewindow.cpp \
    ui/disasmwidget.cpp \
    ui/elidedlabel.cpp \
    ui/exceptiondialog.cpp \
    ui/graphicsinspector.cpp \
    ui/hardwarewindow.cpp \
    ui/mainwindow.cpp \
    ui/memoryviewwidget.cpp \
    ui/nonantialiasimage.cpp \
    ui/prefsdialog.cpp \
    ui/profilewindow.cpp \
    ui/registerwidget.cpp \
    ui/rundialog.cpp \
    ui/savebindialog.cpp \
    ui/searchdialog.cpp \
    ui/showaddressactions.cpp \
    ui/symboltext.cpp \

HEADERS += \
    hardware/hardware_st.h \
    hardware/regs_st.h \
    hardware/tos.h \
    hopper/buffer.h \
    hopper/decode.h \
    hopper/instruction.h \
    hrdbapplication.h \
    models/breakpoint.h \
    models/disassembler.h \
    models/exceptionmask.h \
    models/launcher.h \
    models/memory.h \
    models/profiledata.h \
    models/registers.h \
    models/session.h \
    models/stringformat.h \
    models/stringparsers.h \
    models/stringsplitter.h \
    models/symboltable.h \
    models/symboltablemodel.h \
    models/targetmodel.h \
    models/filewatcher.h \
    transport/dispatcher.h \
    transport/remotecommand.h \
    ui/addbreakpointdialog.h \
    ui/breakpointswidget.h \
    ui/colouring.h \
    ui/consolewindow.h \
    ui/disasmwidget.h \
    ui/elidedlabel.h \
    ui/exceptiondialog.h \
    ui/graphicsinspector.h \
    ui/hardwarewindow.h \
    ui/mainwindow.h \
    ui/memoryviewwidget.h \
    ui/nonantialiasimage.h \
    ui/prefsdialog.h \
    ui/profilewindow.h \
    ui/qtversionwrapper.h \
    ui/quicklayout.h \
    ui/registerwidget.h \
    ui/rundialog.h \
    ui/savebindialog.h \
    ui/searchdialog.h \
    ui/showaddressactions.h \
    ui/symboltext.h

RESOURCES     = hrdb.qrc    

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    docs/README.txt \
    docs/hrdb_release_notes.txt
