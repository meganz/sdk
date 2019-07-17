CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = MEGAsdktests
TEMPLATE = app

CONFIG += USE_LIBUV
CONFIG += USE_MEGAAPI
CONFIG += USE_MEDIAINFO
CONFIG += USE_FFMPEG
CONFIG -= qt

LIBS += -lgtest

include(../../../../bindings/qt/sdk.pri)

SOURCES += ../../../../tests/sdk_test.cpp \
           ../../../../tests/sdktests.cpp

HEADERS += \
    ../../../../tests/sdk_test.h
