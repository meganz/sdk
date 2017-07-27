CONFIG -= qt

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
    QMAKE_LFLAGS += /LARGEADDRESSAWARE
    QMAKE_LFLAGS_WINDOWS += /SUBSYSTEM:WINDOWS,5.01
    QMAKE_LFLAGS_CONSOLE += /SUBSYSTEM:CONSOLE,5.01
    DEFINES += PSAPI_VERSION=1
    DEFINES += UNICODE _UNICODE NTDDI_VERSION=0x05010000 _WIN32_WINNT=0x0501
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

