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
    ../../../../examples/megacmd/megacmdshell/megacmdshellcommunications.cpp

HEADERS += ../../../../examples/megacmd/megacmdshell/megacmdshell.h \
    ../../../../examples/megacmd/megacmdshell/megacmdshellcommunications.h

win32 {
DEFINES += USE_READLINE_STATIC
}

CONFIG += c++11

LIBS += -lreadline

DEFINES -= USE_QT

#SOURCES -= src/thread/qtthread.cpp
#win32{
#    SOURCES += src/thread/win32thread.cpp
#}
#else{
#    SOURCES += src/thread/posixthread.cpp
    LIBS += -lpthread
#}


macx {
    LIBS += $$PWD/../../../../bindings/qt/3rdparty/libs/libreadline.a
#    LIBS += -framework Cocoa -framework SystemConfiguration -framework CoreFoundation -framework Foundation -framework Security
#    LIBS += -lncurses
    QMAKE_CXXFLAGS += -g
}

win32 {
    QMAKE_CXXFLAGS_RELEASE = $$QMAKE_CFLAGS_RELEASE_WITH_DEBUGINFO
    QMAKE_LFLAGS_RELEASE = $$QMAKE_LFLAGS_RELEASE_WITH_DEBUGINFO
}

release {
    DEFINES += NDEBUG
}
