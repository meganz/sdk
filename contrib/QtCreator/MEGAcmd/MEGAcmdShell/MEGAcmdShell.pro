CONFIG -= qt

CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = MEGAcmdShell
TEMPLATE = app
CONFIG += console

SOURCES += ../../../../examples/megacmd/megacmdshell/megacmdshell.cpp \
    ../../../../examples/megacmd/megacmdshell/megacmdshellcommunications.cpp \
    ../../../../examples/megacmd/megacmdshell/megacmdshellcommunicationsnamedpipes.cpp

HEADERS += ../../../../examples/megacmd/megacmdshell/megacmdshell.h \
    ../../../../examples/megacmd/megacmdshell/megacmdshellcommunications.h \
    ../../../../examples/megacmd/megacmdshell/megacmdshellcommunicationsnamedpipes.h

win32 {
DEFINES += USE_READLINE_STATIC
}


CONFIG += c++11

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
    LIBS += $$PWD/../../../../bindings/qt/3rdparty/libs/libreadline.a
#    LIBS += -framework Cocoa -framework SystemConfiguration -framework CoreFoundation -framework Foundation -framework Security
#    LIBS += -lncurses
    QMAKE_CXXFLAGS += -g
}

win32 {
    QMAKE_CXXFLAGS_RELEASE = $$QMAKE_CFLAGS_RELEASE_WITH_DEBUGINFO
    QMAKE_LFLAGS_RELEASE = $$QMAKE_LFLAGS_RELEASE_WITH_DEBUGINFO

    INCLUDEPATH += $$PWD/../../../../bindings/qt/3rdparty/include

    DEFINES += __STDC_LIMIT_MACROS #this is required to include <thread> or <mutex>

    LIBS +=  -lshlwapi -lws2_32 -luser32 -ladvapi32

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

release {
    DEFINES += NDEBUG
}


QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-parameter
