
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
    src/pendingcontactrequest.cpp \
    src/crypto/cryptopp.cpp  \
    src/crypto/sodium.cpp  \
    src/db/sqlite.cpp  \
    src/gfx/qt.cpp \
    src/gfx/external.cpp \
    src/thread/qtthread.cpp \
    src/mega_utf8proc.cpp \
    src/mega_zxcvbn.cpp \
    src/mediafileattribute.cpp

CONFIG(USE_MEGAAPI) {
    SOURCES += src/megaapi.cpp src/megaapi_impl.cpp \
        bindings/qt/QTMegaRequestListener.cpp \
        bindings/qt/QTMegaTransferListener.cpp \
        bindings/qt/QTMegaGlobalListener.cpp \
        bindings/qt/QTMegaSyncListener.cpp \
        bindings/qt/QTMegaListener.cpp \
        bindings/qt/QTMegaEvent.cpp
}

# CONFIG += USE_LIBUV
CONFIG(USE_LIBUV) {
    SOURCES += src/mega_http_parser.cpp
    DEFINES += HAVE_LIBUV
    INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/libuv
    win32 {
        LIBS += -llibuv -lIphlpapi -lUserenv
    }

    unix:!macx {
       exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libuv.a) {
        LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libuv.a
       }
       else {
        LIBS += -luv
       }
    }

    macx {
        LIBS += -luv
    }
}

CONFIG(USE_MEDIAINFO) {
    DEFINES += USE_MEDIAINFO UNICODE

    win32 {
        LIBS += -lMediaInfo -lZenLib -lzlibstat
    }

    mac {
        LIBS += -lmediainfo -lzen -lz
    }

    unix:!macx {

       exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libmediainfo.a) {
        LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libmediainfo.a
       }
       else {
        LIBS += -lmediainfo
       }
       exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libzen.a) {
        LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libzen.a
       }
       else {
        LIBS += -lzen
       }
    }

}

CONFIG(USE_FFMPEG) {
    DEFINES += HAVE_FFMPEG
    INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/ffmpeg

    unix:!macx {
        INCLUDEPATH += /usr/include/ffmpeg
    }

    LIBS += -lavcodec -lavformat -lavutil -lswscale
}

win32 {
    # comment this line to use WinHTTP on Windows
    CONFIG += USE_CURL

    CONFIG(USE_CURL) {
        SOURCES += src/wincurl/net.cpp  \
            src/wincurl/fs.cpp  \
            src/wincurl/waiter.cpp
        HEADERS += include/mega/wincurl/meganet.h
        DEFINES += USE_CURL USE_OPENSSL
        LIBS +=  -llibcurl -lcares -llibeay32 -lssleay32
    }
    else {
        SOURCES += src/win32/net.cpp \
            src/win32/fs.cpp \
            src/win32/waiter.cpp
        HEADERS += include/mega/win32/meganet.h
    }

    # link winhttp anyway (required for automatic proxy detection)
    LIBS += -lwinhttp -ladvapi32
    DEFINES += _CRT_SECURE_NO_WARNINGS
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
            include/mega/pendingcontactrequest.h \
            include/mega/crypto/cryptopp.h  \
            include/mega/crypto/sodium.h  \
            include/mega/db/sqlite.h  \
            include/mega/gfx/qt.h \
            include/mega/gfx/external.h \
            include/mega/thread.h \
            include/mega/thread/cppthread.h \
            include/mega/thread/qtthread.h \
            include/megaapi.h \
            include/megaapi_impl.h \
            include/mega/mega_utf8proc.h \
            include/mega/thread/posixthread.h \
            include/mega/mega_zxcvbn.h \
            include/mega/mediafileattribute.h

CONFIG(USE_MEGAAPI) {
    HEADERS += bindings/qt/QTMegaRequestListener.h \
            bindings/qt/QTMegaTransferListener.h \
            bindings/qt//QTMegaGlobalListener.h \
            bindings/qt/QTMegaSyncListener.h \
            bindings/qt/QTMegaListener.h \
            bindings/qt/QTMegaEvent.h
}

win32 {
    HEADERS  += include/mega/win32/megasys.h  \
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

CONFIG(USE_PCRE) {
 DEFINES += USE_PCRE
}

DEFINES += USE_SQLITE USE_CRYPTOPP USE_QT MEGA_QT_LOGGING ENABLE_SYNC ENABLE_CHAT
INCLUDEPATH += $$MEGASDK_BASE_PATH/include
INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt
INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include

!release {
    DEFINES += SQLITE_DEBUG DEBUG
}
else {
    DEFINES += NDEBUG
}

win32 {
    INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/zlib
    INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/libsodium

    CONFIG(USE_CURL) {
        INCLUDEPATH += $$MEGASDK_BASE_PATH/include/mega/wincurl
        INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/openssl
        INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/cares
    }
    else {
        INCLUDEPATH += $$MEGASDK_BASE_PATH/include/mega/win32
    }

    contains(CONFIG, BUILDX64) {
       release {
            LIBS += -L"$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/x64"
        }
        else {
            LIBS += -L"$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/x64d"
        }
    }

    !contains(CONFIG, BUILDX64) {
        release {
            LIBS += -L"$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/x32"
        }
        else {
            LIBS += -L"$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/x32d"
        }
    }

    CONFIG(USE_PCRE) {
     INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/pcre
     DEFINES += PCRE_STATIC
     LIBS += -lpcre
    }

    LIBS += -lshlwapi -lws2_32 -luser32 -lsodium -lcryptopp -lzlibstat
}

unix:!macx {
   INCLUDEPATH += $$MEGASDK_BASE_PATH/include/mega/posix
   LIBS += -lsqlite3 -lrt

   exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcurl.a) {
    LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcurl.a
   }
   else {
    LIBS += -lcurl
   }

   exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libz.a) {
    LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libz.a
   }
   else {
    LIBS += -lz
   }

   exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libssl.a) {
    LIBS +=  $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libssl.a
   }
   else {
    LIBS += -lssl
   }
   
   exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcrypto.a) {
    LIBS +=  $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcrypto.a
   }
   else {
    LIBS += -lcrypto 
   }

   exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcryptopp.a) {
    LIBS +=  $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcryptopp.a
   }
   else {
    LIBS += -lcryptopp
   }

   exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcares.a) {
    LIBS +=  $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcares.a
   }
   else {
    LIBS += -lcares
   }

   exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libsodium.a) {
    LIBS +=  $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libsodium.a
   }
   else {
    LIBS += -lsodium
   }

   CONFIG(USE_PCRE) {
    DEFINES += PCRE_STATIC
    exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libpcre.a) {
     LIBS +=  $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libpcre.a
    }
    else {
     LIBS += -lpcre
    }
   }
}

macx {
   INCLUDEPATH += $$MEGASDK_BASE_PATH/include/mega/posix
   INCLUDEPATH += $$MEGASDK_BASE_PATH/include/mega/osx

   OBJECTIVE_SOURCES += $$MEGASDK_BASE_PATH/src/osx/osxutils.mm

   SOURCES += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/sqlite3.c

   INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/curl
   INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/libsodium
   INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/openssl
   INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/cares
   INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/mediainfo
   INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/zenlib

   CONFIG(USE_PCRE) {
    INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/pcre
    DEFINES += PCRE_STATIC
    LIBS += -lpcre
   }

   DEFINES += _DARWIN_FEATURE_64_BIT_INODE USE_OPENSSL CRYPTOPP_DISABLE_ASM

   LIBS += -L$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/ $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcares.a $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcurl.a $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libsodium.a \
            -lz -lssl -lcrypto -lcryptopp
   LIBS += -framework SystemConfiguration
}
