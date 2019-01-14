CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = MEGAcli
TEMPLATE = app
CONFIG += console
CONFIG += noreadline

win32 {
    DEFINES += __STDC_LIMIT_MACROS #this is required to include <thread> or <mutex>
}

CONFIG += USE_AUTOCOMPLETE
CONFIG += USE_CONSOLE

SOURCES += ../../../examples/megacli.cpp
HEADERS += ../../../examples/megacli.h
include(../../../bindings/qt/sdk.pri)
