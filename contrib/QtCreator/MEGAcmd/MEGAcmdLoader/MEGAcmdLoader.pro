CONFIG -= qt

CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TARGET = MEGAcmdLoader
TEMPLATE = app
CONFIG += console

SOURCES += ../../../../examples/megacmd/loader/megacmdloader.cpp
