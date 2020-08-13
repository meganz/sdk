CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = tool_purge_account
TEMPLATE = app

CONFIG += USE_LIBUV
CONFIG += USE_MEGAAPI
CONFIG += USE_MEDIAINFO
CONFIG += USE_FFMPEG
CONFIG -= qt
CONFIG += object_parallel_to_source

LIBS += -lgtest

include(../../../../bindings/qt/sdk.pri)

SOURCES += ../../../../tests/tool/purge_account.cpp
