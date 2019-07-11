CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
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

LIBS += -lgtest

include(../../../../bindings/qt/sdk.pri)

SOURCES += \
    ../../../../tests/unit/main.cpp \
    ../../../../tests/unit/Commands_test.cpp \
    ../../../../tests/unit/Crypto_test.cpp \
    ../../../../tests/unit/Serialization_test.cpp \
    ../../../../tests/unit/PayCrypter_test.cpp \
    ../../../../tests/unit/MegaApi_test.cpp
