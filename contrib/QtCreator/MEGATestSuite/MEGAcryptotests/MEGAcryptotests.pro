CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = MEGAcryptotests
TEMPLATE = app

CONFIG += USE_MEGAAPI
CONFIG += USE_LIBUV
CONFIG += USE_MEDIAINFO
CONFIG += USE_LIBRAW
CONFIG += USE_FFMPEG
CONFIG -= qt

LIBS += -lgtest

include(../../../../bindings/qt/sdk.pri)

SOURCES += ../../../../tests/crypto_test.cpp \
           ../../../../tests/tests.cpp  \
           ../../../../tests/paycrypt_test.cpp
