CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = MEGAcli
TEMPLATE = app
CONFIG += console
CONFIG += noreadline

win32 {
    DEFINES += USE_READLINE_STATIC
    DEFINES += __STDC_LIMIT_MACROS #this is required to include <thread> or <mutex>
    CONFIG(noreadline) {
        DEFINES += NO_READLINE
    }
}

LIBS += -lreadline

win32 {
    CONFIG(noreadline) {
        SOURCES += ../../../src/win32/console.cpp
        SOURCES += ../../../src/win32/consolewaiter.cpp
    }
    else {
        SOURCES += ../../../src/wincurl/console.cpp
        SOURCES += ../../../src/wincurl/consolewaiter.cpp
    }
    QMAKE_CXXFLAGS+=/Zc:__cplusplus /std:c++ #this will set _cplusplus correctly in MSVC >= 2017 15.7 Preview 3
}
else {
    SOURCES += ../../../src/posix/console.cpp
    SOURCES += ../../../src/posix/consolewaiter.cpp
}

SOURCES += ../../../examples/megacli.cpp
HEADERS += ../../../examples/megacli.h
include(../../../bindings/qt/sdk.pri)
