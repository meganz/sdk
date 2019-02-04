CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = MEGAsimplesync
TEMPLATE = app
CONFIG += console

SOURCES += ../../../examples/megasimplesync.cpp
include(../../../bindings/qt/sdk.pri)

macx {
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.9
    LIBS += -framework Cocoa
    LIBS += -framework Security
}
