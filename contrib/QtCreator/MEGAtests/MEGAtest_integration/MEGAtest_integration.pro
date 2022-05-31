CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
    CONFIG += ENABLE_WERROR_COMPILATION
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


win32 {
    CONFIG += USE_AUTOCOMPLETE
    CONFIG += console
    debug:LIBS += -lgmockd -lgtestd
    !debug:LIBS += -lgmock -lgtest
}
else {
    CONFIG += object_parallel_to_source
    LIBS += -lgmock -lgtest
}

include(../../../../bindings/qt/sdk.pri)

CONFIG -= c++11
QMAKE_CXXFLAGS-=-std=c++11
CONFIG += c++17
QMAKE_CXXFLAGS+=-std=c++17

SOURCES += \
../../../../tests/integration/main.cpp \
../../../../tests/integration/SdkTest_test.cpp \
../../../../tests/integration/Sync_test.cpp

HEADERS += \
../../../../tests/integration/test.h \
../../../../tests/integration/SdkTest_test.h

copydata.commands = $(COPY_DIR) $$shell_path($$PWD/../../../../tests/integration/test_cover_png.mp3) $$OUT_PWD
first.depends = $(first) copydata
export(first.depends)
export(copydata.commands)
QMAKE_EXTRA_TARGETS += first copydata
