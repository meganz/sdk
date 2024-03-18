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

unix:!macx {
    exists(/usr/include/fpdfview.h) {
        CONFIG += USE_PDFIUM
    }
}
else {
    CONFIG += USE_PDFIUM
}

win32 {
    CONFIG += USE_AUTOCOMPLETE
    CONFIG += console
}
else {
    CONFIG += object_parallel_to_source
}

include(../../../../bindings/qt/sdk.pri)

vcpkg {
    debug:LIBS += -lgmockd -lgtestd
    !debug:LIBS += -lgmock -lgtest
}
else {
    LIBS += -lgmock -lgtest
}

CONFIG -= c++11
QMAKE_CXXFLAGS-=-std=c++11
CONFIG += c++17
QMAKE_CXXFLAGS+=-std=c++17

SOURCES += \
$$MEGASDK_BASE_PATH/tests/gtest_common.cpp \
$$MEGASDK_BASE_PATH/tests/sdk_test_utils.cpp \
$$MEGASDK_BASE_PATH/tests/integration/main.cpp \
$$MEGASDK_BASE_PATH/tests/integration/SdkTest_test.cpp \
$$MEGASDK_BASE_PATH/tests/integration/Sync_test.cpp

HEADERS += \
$$MEGASDK_BASE_PATH/tests/gtest_common.h \
$$MEGASDK_BASE_PATH/tests/sdk_test_utils.h \
$$MEGASDK_BASE_PATH/tests/integration/test.h \
$$MEGASDK_BASE_PATH/tests/integration/SdkTest_test.h

INCLUDEPATH += $$MEGASDK_BASE_PATH/tests

copydata.commands = $(COPY_DIR) $$shell_path($$MEGASDK_BASE_PATH/tests/integration/test-data/*) $$shell_path($$OUT_PWD)
first.depends = $(first) copydata
export(first.depends)
export(copydata.commands)
QMAKE_EXTRA_TARGETS += first copydata

macx {
    # At least 10.15 required.
    contains(QT_ARCH, arm64):QMAKE_MACOSX_DEPLOYMENT_TARGET = 11.0
    else:QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.15
    LIBS += -framework Cocoa
}
