CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = MEGAcmd
TEMPLATE = app
CONFIG += console
CONFIG += USE_MEGAAPI

win32 {
DEFINES += USE_READLINE_STATIC
}

LIBS += -lreadline

packagesExist(libpcrecpp){
DEFINES += USE_PCRE
LIBS += -lpcrecpp
}

win32 {
    SOURCES += ../../../src/wincurl/console.cpp
    SOURCES += ../../../src/wincurl/consolewaiter.cpp
}
else {
    SOURCES += ../../../src/posix/console.cpp
    SOURCES += ../../../src/posix/consolewaiter.cpp
}

SOURCES += ../../../examples/megacmd/megacmd.cpp \
    ../../../examples/megacmd/listeners.cpp \
    ../../../examples/megacmd/megacmdexecuter.cpp \
    ../../../examples/megacmd/megacmdlogger.cpp \
    ../../../examples/megacmd/configurationmanager.cpp \
    ../../../examples/megacmd/comunicationsmanager.cpp \
    ../../../examples/megacmd/megacmdutils.cpp \
    ../../../examples/megacmd/synchronousrequestlistener.cpp
HEADERS += ../../../examples/megacmd/megacmd.h \
    ../../../examples/megacmd/megacmdexecuter.h \
    ../../../examples/megacmd/listeners.h \
    ../../../examples/megacmd/megacmdlogger.h \
    ../../../examples/megacmd/configurationmanager.h \
    ../../../examples/megacmd/comunicationsmanager.h \
    ../../../examples/megacmd/megacmdutils.h \
    ../../../examples/megacmd/synchronousrequestlistener.h

include(../../../bindings/qt/sdk.pri)
