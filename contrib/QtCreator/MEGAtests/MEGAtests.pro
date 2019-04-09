

CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = MEGASyncTests
TEMPLATE = app

CONFIG += USE_AUTOCOMPLETE
CONFIG += USE_MEDIAINFO
CONFIG += USE_FFMPEG
CONFIG -= qt

CONFIG += c++17
LIBS+=-lstdc++fs
QMAKE_CXXFLAGS+=-std=c++17

LIBS+=-lgtest

include(../../../bindings/qt/sdk.pri)
SOURCES += ../../../tests/synctests.cpp

