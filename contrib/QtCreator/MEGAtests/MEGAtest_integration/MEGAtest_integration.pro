CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = test_integration
TEMPLATE = app

CONFIG += USE_LIBUV
CONFIG += USE_MEGAAPI
CONFIG += USE_MEDIAINFO
CONFIG += USE_LIBRAW
CONFIG += USE_FFMPEG
CONFIG -= qt

LIBS += -lgtest

include(../../../../bindings/qt/sdk.pri)

CONFIG -= c++11
QMAKE_CXXFLAGS-=-std=c++11
CONFIG += c++17
QMAKE_CXXFLAGS+=-std=c++17

SOURCES += ../../../../tests/integration/main.cpp \
           ../../../../tests/integration/sdk_test.cpp \
           ../../../../tests/integration/sync_test.cpp

HEADERS += ../../../../tests/integration/sdk_test.h \
           ../../../../tests/integration/test.h
