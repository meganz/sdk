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

CONFIG += USE_MEGAAPI
CONFIG += USE_MEDIAINFO
CONFIG += USE_LIBRAW
CONFIG += USE_FFMPEG
CONFIG -= qt

LIBS += -lgtest

include(../../../../bindings/qt/sdk.pri)

SOURCES += ../../../../tests/unit/commands_test.cpp \
           ../../../../tests/unit/crypto_test.cpp  \
           ../../../../tests/unit/json_test.cpp \
           ../../../../tests/unit/main.cpp \
           ../../../../tests/unit/megaapi_test.cpp \
           ../../../../tests/unit/paycrypt_test.cpp
