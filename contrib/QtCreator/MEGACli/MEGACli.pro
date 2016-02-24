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

DEFINES += USE_READLINE_STATIC
LIBS += -lreadline

SOURCES += ../../../src/wincurl/console.cpp
SOURCES += ../../../src/wincurl/consolewaiter.cpp
SOURCES += ../../../examples/megacli.cpp
include(../../../bindings/qt/sdk.pri)
