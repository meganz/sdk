CONFIG(debug, debug|release) {
    CONFIG -= debug release
    CONFIG += debug
}
CONFIG(release, debug|release) {
    CONFIG -= debug release
    CONFIG += release
}

TEMPLATE = subdirs
SUBDIRS = MEGAcmdServer MEGAcmdClient

macx {
    SUBDIRS += MEGAcmdLoader
}
