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

CONFIG(WITH_FUSE) {
    INCLUDEPATH += $$MEGASDK_BASE_PATH

    FUSE_COMMON_TESTING_SRC = $$MEGASDK_BASE_PATH/src/fuse/common/testing
    FUSE_COMMON_TESTING_INC = $$MEGASDK_BASE_PATH/src/fuse/common/fuse/testing

    HEADERS += \
        $$FUSE_COMMON_TESTING_INC/client.h \
        $$FUSE_COMMON_TESTING_INC/cloud_path.h \
        $$FUSE_COMMON_TESTING_INC/cloud_path_forward.h \
        $$FUSE_COMMON_TESTING_INC/directory.h \
        $$FUSE_COMMON_TESTING_INC/file.h \
        $$FUSE_COMMON_TESTING_INC/mock_client.h \
        $$FUSE_COMMON_TESTING_INC/model.h \
        $$FUSE_COMMON_TESTING_INC/model_forward.h \
        $$FUSE_COMMON_TESTING_INC/mount_event_observer.h \
        $$FUSE_COMMON_TESTING_INC/mount_event_observer_forward.h \
        $$FUSE_COMMON_TESTING_INC/parameters.h \
        $$FUSE_COMMON_TESTING_INC/parameters_forward.h \
        $$FUSE_COMMON_TESTING_INC/path.h \
        $$FUSE_COMMON_TESTING_INC/path_forward.h \
        $$FUSE_COMMON_TESTING_INC/printers.h \
        $$FUSE_COMMON_TESTING_INC/real_client.h \
        $$FUSE_COMMON_TESTING_INC/test_base.h \
        $$FUSE_COMMON_TESTING_INC/test.h \
        $$FUSE_COMMON_TESTING_INC/utility.h \
        $$FUSE_COMMON_TESTING_INC/watchdog.h

    SOURCES += \
        $$FUSE_COMMON_TESTING_SRC/client.cpp \
        $$FUSE_COMMON_TESTING_SRC/cloud_path.cpp \
        $$FUSE_COMMON_TESTING_SRC/common_tests.cpp \
        $$FUSE_COMMON_TESTING_SRC/directory.cpp \
        $$FUSE_COMMON_TESTING_SRC/file.cpp \
        $$FUSE_COMMON_TESTING_SRC/mock_client.cpp \
        $$FUSE_COMMON_TESTING_SRC/model.cpp \
        $$FUSE_COMMON_TESTING_SRC/mount_event_observer.cpp \
        $$FUSE_COMMON_TESTING_SRC/mount_tests.cpp \
        $$FUSE_COMMON_TESTING_SRC/parameters.cpp \
        $$FUSE_COMMON_TESTING_SRC/path.cpp \
        $$FUSE_COMMON_TESTING_SRC/printers.cpp \
        $$FUSE_COMMON_TESTING_SRC/real_client.cpp \
        $$FUSE_COMMON_TESTING_SRC/sync_tests.cpp \
        $$FUSE_COMMON_TESTING_SRC/test.cpp \
        $$FUSE_COMMON_TESTING_SRC/test_base.cpp \
        $$FUSE_COMMON_TESTING_SRC/utility.cpp \
        $$FUSE_COMMON_TESTING_SRC/watchdog.cpp

    FUSE_SUPPORTED_TESTING_INC = \
        $$MEGASDK_BASE_PATH/src/fuse/supported/fuse/platform/testing

    FUSE_SUPPORTED_TESTING_SRC = \
        $$MEGASDK_BASE_PATH/src/fuse/supported/testing

    HEADERS += \
        $$FUSE_SUPPORTED_TESTING_INC/platform_tests.h

    SOURCES += \
        $$FUSE_SUPPORTED_TESTING_SRC/platform_tests.cpp

    unix {
        FUSE_POSIX_SRC = $$MEGASDK_BASE_PATH/src/fuse/supported/posix

        FUSE_POSIX_TESTING_SRC = $$FUSE_POSIX_SRC/testing
        FUSE_POSIX_TESTING_INC = $$FUSE_POSIX_SRC/fuse/testing

        HEADERS += \
            $$FUSE_POSIX_TESTING_INC/printers.h \
            $$FUSE_POSIX_TESTING_INC/wrappers.h

        SOURCES += \
            $$FUSE_POSIX_TESTING_SRC/platform_tests.cpp \
            $$FUSE_POSIX_TESTING_SRC/printers.cpp \
            $$FUSE_POSIX_TESTING_SRC/utility.cpp \
            $$FUSE_POSIX_TESTING_SRC/wrappers.cpp

        !macx:SOURCES += $$FUSE_POSIX_SRC/linux/platform_tests.cpp
    } # unix

    win32 {
        FUSE_WINDOWS_SRC = $$MEGASDK_BASE_PATH/src/fuse/supported/windows

        FUSE_WINDOWS_TESTING_SRC = $$FUSE_WINDOWS_SRC/testing
        FUSE_WINDOWS_TESTING_INC = $$FUSE_WINDOWS_SRC/fuse/testing

        HEADERS += \
            $$FUSE_WINDOWS_TESTING_INC/directory_monitor.h \
            $$FUSE_WINDOWS_TESTING_INC/printers.h \
            $$FUSE_WINDOWS_TESTING_INC/wrappers.h

        SOURCES += \
            $$FUSE_WINDOWS_TESTING_SRC/directory_monitor.cpp \
            $$FUSE_WINDOWS_TESTING_SRC/platform_tests.cpp \
            $$FUSE_WINDOWS_TESTING_SRC/printers.cpp \
            $$FUSE_WINDOWS_TESTING_SRC/utility.cpp \
            $$FUSE_WINDOWS_TESTING_SRC/wrappers.cpp
    } # win32
} # WITH_FUSE

