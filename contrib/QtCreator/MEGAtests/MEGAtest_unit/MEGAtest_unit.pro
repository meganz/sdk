CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
    CONFIG += ENABLE_WERROR_COMPILATION
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = test_unit
TEMPLATE = app

CONFIG += USE_LIBUV
CONFIG += USE_MEGAAPI
CONFIG += USE_MEDIAINFO
CONFIG += USE_LIBRAW
CONFIG += USE_FFMPEG
CONFIG -= qt
CONFIG += object_parallel_to_source

win32 {
    CONFIG += USE_AUTOCOMPLETE
    CONFIG += console
    debug:LIBS += -lgmockd -lgtestd
    !debug:LIBS += -lgmock -lgtest
}
else {
    LIBS += -lgmock -lgtest
}

include(../../../../bindings/qt/sdk.pri)

SOURCES += \
../../../../tests/unit/AttrMap_test.cpp \
../../../../tests/unit/ChunkMacMap_test.cpp \
../../../../tests/unit/Commands_test.cpp \
../../../../tests/unit/Crypto_test.cpp \
../../../../tests/unit/FileFingerprint_test.cpp \
../../../../tests/unit/File_test.cpp \
../../../../tests/unit/FsNode.cpp \
../../../../tests/unit/Logging_test.cpp \
../../../../tests/unit/main.cpp \
../../../../tests/unit/MediaProperties_test.cpp \
../../../../tests/unit/MegaApi_test.cpp \
../../../../tests/unit/PayCrypter_test.cpp \
../../../../tests/unit/PendingContactRequest_test.cpp \
../../../../tests/unit/Serialization_test.cpp \
../../../../tests/unit/Share_test.cpp \
../../../../tests/unit/Sync_test.cpp \
../../../../tests/unit/TextChat_test.cpp \
../../../../tests/unit/Transfer_test.cpp \
../../../../tests/unit/User_test.cpp \
../../../../tests/unit/utils.cpp \
../../../../tests/unit/utils_test.cpp

HEADERS += \
../../../../tests/unit/constants.h \
../../../../tests/unit/DefaultedDbTable.h \
../../../../tests/unit/DefaultedDirAccess.h \
../../../../tests/unit/DefaultedFileAccess.h \
../../../../tests/unit/DefaultedFileSystemAccess.h \
../../../../tests/unit/FsNode.h \
../../../../tests/unit/NotImplemented.h \
../../../../tests/unit/utils.h
