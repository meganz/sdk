CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
    CONFIG += ENABLE_WERROR_COMPILATION
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = MEGAsimplesync
TEMPLATE = app
CONFIG += console
CONFIG -= qt

CONFIG += USE_MEDIAINFO

CONFIG += USE_MEDIAINFO
CONFIG += USE_LIBRAW
CONFIG += USE_FFMPEG

unix:!macx {
    exists(/usr/include/fpdfview.h) {
        CONFIG += USE_PDFIUM
    }
}
else {
    CONFIG += USE_PDFIUM
}

SOURCES += ../../../examples/megasimplesync.cpp
include(../../../bindings/qt/sdk.pri)

macx {
    contains(QT_ARCH, arm64):QMAKE_MACOSX_DEPLOYMENT_TARGET = 11.0
    else:QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.12
    LIBS += -framework Cocoa
    LIBS += -framework Security
}
