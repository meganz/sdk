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

SOURCES += ../../../../examples/megacmd/client/megacmdclient.cpp \
    ../../../../examples/megacmd/megacmdshell/megacmdshellcommunications.cpp \
    ../../../../examples/megacmd/megacmdshell/megacmdshellcommunicationsnamedpipes.cpp

HEADERS += ../../../../examples/megacmd/megacmdshell/megacmdshellcommunications.h \
    ../../../../examples/megacmd/megacmdshell/megacmdshellcommunicationsnamedpipes.h


win32 {
    LIBS +=  -lshlwapi -lws2_32
    LIBS +=  -lshell32 -luser32 -ladvapi32

    RC_FILE = icon.rc
    QMAKE_CXXFLAGS_RELEASE = $$QMAKE_CFLAGS_RELEASE_WITH_DEBUGINFO
    QMAKE_LFLAGS_RELEASE = $$QMAKE_LFLAGS_RELEASE_WITH_DEBUGINFO
}
else{
    LIBS += -lpthread
    QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-parameter
}

CONFIG += c++11

macx {
    QMAKE_CXXFLAGS += -g
}

release {
    DEFINES += NDEBUG
}

