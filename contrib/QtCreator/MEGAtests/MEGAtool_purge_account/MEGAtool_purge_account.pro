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


win32 {
    CONFIG += console
}
else {
    CONFIG += object_parallel_to_source
}

include(../../../../bindings/qt/sdk.pri)

vcpkg {
    debug:LIBS += -lgtestd
    !debug:LIBS += -lgtest
}
else {
    LIBS += -lgtest
}

SOURCES += ../../../../tests/tool/purge_account.cpp

macx {
    LIBS += -framework Cocoa
}
