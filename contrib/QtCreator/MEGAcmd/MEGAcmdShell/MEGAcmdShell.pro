CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
    DEFINES += NDEBUG
}

CONFIG -= qt

TARGET = MEGAcmdShell
TEMPLATE = app
CONFIG += console

SOURCES += ../../../../examples/megacmd/megacmdshell/megacmdshell.cpp \
    ../../../../examples/megacmd/megacmdshell/megacmdshellcommunications.cpp \
    ../../../../examples/megacmd/megacmdshell/megacmdshellcommunicationsnamedpipes.cpp


HEADERS += ../../../../examples/megacmd/megacmdshell/megacmdshell.h \
    ../../../../examples/megacmd/megacmdshell/megacmdshellcommunications.h \
    ../../../../examples/megacmd/megacmdshell/megacmdshellcommunicationsnamedpipes.h \
    ../../../../include/mega/thread.h

INCLUDEPATH += ../../../../include

win32{
    QMAKE_LFLAGS += /LARGEADDRESSAWARE
    QMAKE_LFLAGS_WINDOWS += /SUBSYSTEM:WINDOWS,5.01
    QMAKE_LFLAGS_CONSOLE += /SUBSYSTEM:CONSOLE,5.01
    DEFINES += PSAPI_VERSION=1
    DEFINES += UNICODE _UNICODE NTDDI_VERSION=0x05010000 _WIN32_WINNT=0x0501
}


win32{
    DEFINES += USE_WIN32THREAD
}
else{
    ## Disable de following to work with posix threads
    #DEFINES+=USE_CPPTHREAD
    #CONFIG += c++11

    DEFINES+=USE_PTHREAD
}

win32 {
SOURCES += ../../../../src/thread/win32thread.cpp \
    ../../../../src/logging.cpp
HEADERS +=  ../../../../include/mega/win32thread.h \
    ../../../../include/mega/logging.h
}
else {
SOURCES += ../../../../src/thread/cppthread.cpp \
    ../../../../src/thread/posixthread.cpp \
    ../../../../src/logging.cpp

HEADERS +=  ../../../../include/mega/posixthread.h \
    ../../../../include/mega/thread/cppthread.h \
    ../../../../include/mega/logging.h
}


win32 {
DEFINES += USE_READLINE_STATIC
}

LIBS += -lreadline

DEFINES -= USE_QT

#SOURCES -= src/thread/qtthread.cpp
win32{
#    SOURCES += src/thread/win32thread.cpp
}
else{
#    SOURCES += src/thread/posixthread.cpp
    LIBS += -lpthread
}

macx {
    INCLUDEPATH += $$PWD/../../../../bindings/qt/3rdparty/include
    LIBS += $$PWD/../../../../bindings/qt/3rdparty/libs/libreadline.a
    LIBS += -framework Cocoa -framework SystemConfiguration -framework CoreFoundation -framework Foundation -framework Security
    LIBS += -lncurses
    QMAKE_CXXFLAGS += -g
}

win32 {
    QMAKE_CXXFLAGS_RELEASE = $$QMAKE_CFLAGS_RELEASE_WITH_DEBUGINFO
    QMAKE_LFLAGS_RELEASE = $$QMAKE_LFLAGS_RELEASE_WITH_DEBUGINFO

    INCLUDEPATH += $$PWD/../../../../bindings/qt/3rdparty/include

    DEFINES += __STDC_LIMIT_MACROS #this is required to include <thread> or <mutex>

    LIBS +=  -lshlwapi -lws2_32 -luser32 -ladvapi32 -lshell32

    contains(CONFIG, BUILDX64) {
       release {
            LIBS += -L"$$PWD/../../../../bindings/qt/3rdparty/libs/x64"
        }
        else {
            LIBS += -L"$$PWD/../../../../bindings/qt/3rdparty/libs/x64d"
        }
    }


    !contains(CONFIG, BUILDX64) {
        release {
            LIBS += -L"$$PWD/../../../../bindings/qt/3rdparty/libs/x32"
        }
        else {
            LIBS += -L"$$PWD/../../../../bindings/qt/3rdparty/libs/x32d"
        }
    }
}
else {
    QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-parameter
}


