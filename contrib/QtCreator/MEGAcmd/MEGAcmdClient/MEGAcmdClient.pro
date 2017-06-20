CONFIG -= qt

CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = MEGAclient
TEMPLATE = app
CONFIG += console

SOURCES += ../../../../examples/megacmd/client/megacmdclient.cpp

win32 {
    LIBS +=  -lshlwapi -lws2_32
    LIBS +=  -lshell32 -luser32
    RC_FILE = icon.rc
    QMAKE_CXXFLAGS_RELEASE = $$QMAKE_CFLAGS_RELEASE_WITH_DEBUGINFO
    QMAKE_LFLAGS_RELEASE = $$QMAKE_LFLAGS_RELEASE_WITH_DEBUGINFO
}

macx {
    QMAKE_CXXFLAGS += -g
}

release {
    DEFINES += NDEBUG
}
