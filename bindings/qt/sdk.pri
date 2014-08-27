
MEGASDK_BASE_PATH = $$PWD/../../

VPATH += $$MEGASDK_BASE_PATH
SOURCES += src/attrmap.cpp \
    src/backofftimer.cpp \
    src/base64.cpp \
    src/command.cpp \
    src/commands.cpp \
    src/db.cpp \
    src/gfx.cpp \
    src/file.cpp \
    src/fileattributefetch.cpp \
    src/filefingerprint.cpp \
    src/filesystem.cpp \
    src/http.cpp \
    src/json.cpp \
    src/megaclient.cpp \
    src/node.cpp \
    src/pubkeyaction.cpp \
    src/request.cpp \
    src/serialize64.cpp \
    src/share.cpp \
    src/sharenodekeys.cpp \
    src/sync.cpp \
    src/transfer.cpp \
    src/transferslot.cpp \
    src/treeproc.cpp \
    src/user.cpp \
    src/utils.cpp \
    src/logging.cpp \
    src/waiterbase.cpp  \
    src/proxy.cpp \
    src/crypto/cryptopp.cpp  \
    src/crypto/sodium.cpp  \
    src/db/sqlite.cpp  \
    src/gfx/qt.cpp \
    src/gfx/external.cpp \
    src/thread/qtthread.cpp \
    src/megaapi.cpp \
    src/megaapi_impl.cpp \
    bindings/qt/QTMegaRequestListener.cpp \
    bindings/qt/QTMegaTransferListener.cpp \
    bindings/qt/QTMegaListener.cpp \
    third_party/utf8proc/utf8proc.cpp

win32 {
SOURCES += src/win32/net.cpp  \
    src/win32/fs.cpp  \
    src/win32/waiter.cpp
}

unix {
SOURCES += src/posix/net.cpp  \
    src/posix/fs.cpp  \
    src/posix/waiter.cpp
}

HEADERS  += include/mega.h \
            include/mega/account.h \
            include/mega/attrmap.h \
            include/mega/backofftimer.h \
            include/mega/base64.h \
            include/mega/command.h \
            include/mega/console.h \
            include/mega/db.h \
            include/mega/gfx.h \
            include/mega/file.h \
            include/mega/fileattributefetch.h \
            include/mega/filefingerprint.h \
            include/mega/filesystem.h \
            include/mega/http.h \
            include/mega/json.h \
            include/mega/megaapp.h \
            include/mega/megaclient.h \
            include/mega/node.h \
            include/mega/pubkeyaction.h \
            include/mega/request.h \
            include/mega/serialize64.h \
            include/mega/share.h \
            include/mega/sharenodekeys.h \
            include/mega/sync.h \
            include/mega/transfer.h \
            include/mega/transferslot.h \
            include/mega/treeproc.h \
            include/mega/types.h \
            include/mega/user.h \
            include/mega/utils.h \
            include/mega/logging.h \
            include/mega/waiter.h \
            include/mega/proxy.h \
            include/mega/crypto/cryptopp.h  \
            include/mega/crypto/sodium.h  \
            include/mega/db/sqlite.h  \
            include/mega/gfx/qt.h \
            include/mega/gfx/external.h \
            include/mega/thread.h \
            include/mega/thread/qtthread.h \
            include/megaapi.h \
            include/megaapi_impl.h \
            bindings/qt/QTMegaRequestListener.h \
            bindings/qt/QTMegaTransferListener.h \
            bindings/qt/QTMegaListener.h \
            third_party/utf8proc/utf8proc.h

win32 {
    HEADERS  += include/mega/win32/meganet.h  \
            include/mega/win32/megasys.h  \
            include/mega/win32/megafs.h  \
            include/mega/win32/megawaiter.h

    SOURCES += bindings/qt/3rdparty/libs/sqlite3.c
}

unix {
    !exists($$MEGASDK_BASE_PATH/include/mega/config.h) {
        error("Configuration file not found! Please re-run configure script located in the project's root directory!")
    }
    HEADERS  += include/mega/posix/meganet.h  \
            include/mega/posix/megasys.h  \
            include/mega/posix/megafs.h  \
            include/mega/posix/megawaiter.h \
            include/mega/config.h
}

DEFINES += USE_SQLITE USE_CRYPTOPP USE_SODIUM USE_QT MEGA_QT_LOGGING
LIBS += -lcryptopp
INCLUDEPATH += $$MEGASDK_BASE_PATH/include
INCLUDEPATH += $$MEGASDK_BASE_PATH/third_party/utf8proc
INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt
INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include

!release {
    DEFINES += SQLITE_DEBUG DEBUG
}
else {
    DEFINES += NDEBUG
}

win32 {
    INCLUDEPATH += $$[QT_INSTALL_PREFIX]/src/3rdparty/zlib
    INCLUDEPATH += $$MEGASDK_BASE_PATH/include/mega/win32
    INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/cryptopp
    INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/libsodium
    DEFINES += SODIUM_STATIC PCRE_STATIC
    LIBS += -lsodium

    contains(CONFIG, BUILDX64) {
	release {
            LIBS += -L"$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/static_x64"
	}
	else {
            LIBS += -L"$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/staticd_x64"
	}
    }

    !contains(CONFIG, BUILDX64) {
	release {
            LIBS += -L"$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/static"
	}
	else {
            LIBS += -L"$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/staticd"
	}
    }

    LIBS += -lwinhttp -lws2_32 -luser32 -lpcre
}

unix:!macx {
   INCLUDEPATH += $$MEGASDK_BASE_PATH/include/mega/posix
   INCLUDEPATH += /usr/include/cryptopp

   LIBS += -lsqlite3 -lrt

   exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcurl.a) {
    INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include
    LIBS += -L$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/ $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcurl.a -lz -lssl -lcrypto -lcares
   }
   else {
    LIBS += -lcurl
   }

   exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libsodium.a) {
    DEFINES += SODIUM_STATIC PCRE_STATIC
    INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/sodium
    LIBS += -L$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/ $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libsodium.a
   }
   else {
    LIBS += -lsodium
   }

}

macx {
   INCLUDEPATH += $$MEGASDK_BASE_PATH/include/mega/posix
   INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/cryptopp
   SOURCES += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/qt/libs/sqlite3.c
   INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/curl

   exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libsodium.a) {
    DEFINES += SODIUM_STATIC PCRE_STATIC
    INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/sodium
    LIBS += -L$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/ $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libsodium.a
   }
   else {
    LIBS += -lsodium
   }

   LIBS += -L$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/ $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcares.a $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcurl.a -lz -lssl -lcrypto
}
