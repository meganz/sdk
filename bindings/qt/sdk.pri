
CONFIG += object_parallel_to_source

MEGASDK_BASE_PATH = $$PWD/../../

# Define MEGA_USE_C_ARES by default. Allow disabling c-ares code
# by defining env var MEGA_USE_C_ARES=no before running qmake.
ENV_MEGA_USE_C_ARES=$$(MEGA_USE_C_ARES)
!equals(ENV_MEGA_USE_C_ARES, "no") {
DEFINES += MEGA_USE_C_ARES
}

THIRDPARTY_VCPKG_PATH = $$THIRDPARTY_VCPKG_BASE_PATH/vcpkg/installed/$$VCPKG_TRIPLET
exists($$THIRDPARTY_VCPKG_PATH) {
   CONFIG += vcpkg
}
vcpkg:debug:message("Building DEBUG with VCPKG 3rdparty at $$THIRDPARTY_VCPKG_PATH")
vcpkg:release:message("Building RELEASE with VCPKG 3rdparty at $$THIRDPARTY_VCPKG_PATH")
!vcpkg:message("vcpkg not used")

debug:DEBUG_SUFFIX = "d"
else:DEBUG_SUFFIX = ""
debug:DASH_DEBUG_SUFFIX = "-d"
else:DASH_DEBUG_SUFFIX = ""
debug:UNDERSCORE_DEBUG_SUFFIX = "_d"
else:UNDERSCORE_DEBUG_SUFFIX = ""

debug:win32:DEBUG_SUFFIX_WO = "d"
else:DEBUG_SUFFIX_WO = ""

MI_DEBUG_SUFFIX = ""
debug:macx:MI_DEBUG_SUFFIX = "_debug"
debug:win32:MI_DEBUG_SUFFIX = "d"

VPATH += $$MEGASDK_BASE_PATH
SOURCES += src/arguments.cpp \
    src/attrmap.cpp \
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
    src/process.cpp \
    src/pubkeyaction.cpp \
    src/request.cpp \
    src/serialize64.cpp \
    src/nodemanager.cpp \
    src/setandelement.cpp \
    src/share.cpp \
    src/sharenodekeys.cpp \
    src/sync.cpp \
    src/syncfilter.cpp \
    src/transfer.cpp \
    src/transferslot.cpp \
    src/treeproc.cpp \
    src/user.cpp \
    src/useralerts.cpp \
    src/utils.cpp \
    src/logging.cpp \
    src/waiterbase.cpp  \
    src/proxy.cpp \
    src/pendingcontactrequest.cpp \
    src/textchat.cpp \
    src/crypto/cryptopp.cpp  \
    src/crypto/sodium.cpp  \
    src/db/sqlite.cpp  \
    src/gfx/external.cpp \
    src/mega_utf8proc.cpp \
    src/mega_ccronexpr.cpp \
    src/mega_evt_tls.cpp \
    src/mega_zxcvbn.cpp \
    src/mediafileattribute.cpp \
    src/raid.cpp \
    src/raidproxy.cpp \
    src/testhooks.cpp \
    src/heartbeats.cpp

CONFIG(USE_MEGAAPI) {
  SOURCES += src/megaapi.cpp src/megaapi_impl.cpp


  CONFIG(qt) {
    SOURCES += bindings/qt/QTMegaRequestListener.cpp \
        bindings/qt/QTMegaTransferListener.cpp \
        bindings/qt/QTMegaGlobalListener.cpp \
        bindings/qt/QTMegaListener.cpp \
        bindings/qt/QTMegaEvent.cpp
  }
}

!win32 {
    QMAKE_CXXFLAGS += -std=c++11 -Wall -Wextra -Wconversion -Wno-unused-parameter

    unix:!macx {
        GCC_VERSION = $$system("g++ -dumpversion")
        !lessThan(GCC_VERSION, 5) {
            LIBS += -lstdc++fs
        }
    }
}
else {
    # flags synced with contrib/cmake/CMakeLists.txt
    CONFIG += c++17
    QMAKE_CXXFLAGS_WARN_ON ~= s/-W3/-W4
    QMAKE_CXXFLAGS_WARN_ON -= -w34100 -w44458 -w44456
    QMAKE_CXXFLAGS += /wd4201 /wd4100 /wd4706 /wd4458 /wd4324 /wd4456 /wd4266
}

CONFIG(ENABLE_WERROR_COMPILATION) {
    !win32 {
        QMAKE_CXXFLAGS += -Werror
        # Warnings which should not be promoted to errors, but still appear as warnings
        QMAKE_CXXFLAGS += -Wno-error=deprecated-declarations
    }
    else {
        QMAKE_CXXFLAGS += /WX
    }
}

CONFIG(USE_AUTOCOMPLETE) {
    SOURCES += src/autocomplete.cpp
    HEADERS += include/mega/autocomplete.h
}

CONFIG(USE_POLL) {
    DEFINES += USE_POLL
}

CONFIG(USE_CONSOLE) {
    win32 {

        HEADERS += include/mega/win32/megaconsole.h
        HEADERS += include/mega/win32/megaconsolewaiter.h

        CONFIG(noreadline) {
            DEFINES += NO_READLINE
            SOURCES += src/win32/console.cpp
            SOURCES += src/win32/consolewaiter.cpp
        }
        else {
            DEFINES += USE_READLINE_STATIC
            SOURCES += src/wincurl/console.cpp
            SOURCES += src/wincurl/consolewaiter.cpp
            LIBS += -lreadline
        }
        QMAKE_CXXFLAGS+=/Zc:__cplusplus /std:c++ #this will set _cplusplus correctly in MSVC >= 2017 15.7 Preview 3
    }
    else {
        HEADERS += include/mega/posix/megaconsole.h
        HEADERS += include/mega/posix/megaconsolewaiter.h
        SOURCES += src/posix/console.cpp
        SOURCES += src/posix/consolewaiter.cpp
        LIBS += -lreadline
        macx:vcpkg{
            debug:LIBS += $$THIRDPARTY_VCPKG_PATH/debug/lib/libhistory.a
            !debug:LIBS += $$THIRDPARTY_VCPKG_PATH/lib/libhistory.a
            LIBS += -ltermcap
        }
    }
}

CONFIG(ENABLE_CHAT) {
    CONFIG += USE_LIBUV

    !macx {
        !vcpkg:exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libwebsockets.a) {
        LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libwebsockets.a -lcap
        }
        else {
            vcpkg:LIBS += $$THIRDPARTY_VCPKG_PATH/lib/libwebsockets.a -lcap
            !vcpkg:LIBS += -lwebsockets -lcap
        }
    }
    else {
        exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libwebsockets.a) {
        LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libwebsockets.a
        }
        else {
        LIBS += -lwebsockets
        }
    }
}

CONFIG(USE_LIBUV) {
    SOURCES += src/mega_http_parser.cpp
    DEFINES += HAVE_LIBUV
    vcpkg:INCLUDEPATH_EXTERNAL += $$THIRDPARTY_VCPKG_PATH/include/libuv
    !vcpkg:INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/libuv
    win32 {
        LIBS += -llibuv -lIphlpapi -lUserenv -lpsapi -ldbghelp
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

    vcpkg:LIBS += -lmediainfo$$MI_DEBUG_SUFFIX -lzen$$MI_DEBUG_SUFFIX -ltinyxml2
    vcpkg:win32:LIBS += -lzlib$$DEBUG_SUFFIX
    vcpkg:!win32:LIBS += -lz

    !vcpkg:win32 {
        vcpkg:LIBS += -lmediainfo$$DEBUG_SUFFIX -lzen$$DEBUG_SUFFIX -lzlib$$DEBUG_SUFFIX
        else:LIBS += -lMediaInfo -lZenLib -lzlibstat
    }

    !vcpkg:macx {
        LIBS += -lmediainfo -lzen -lz
    }

    !vcpkg:unix:!macx {

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

CONFIG(USE_LIBRAW) {
    DEFINES += HAVE_LIBRAW

    vcpkg:LIBS += -lraw_r$$DEBUG_SUFFIX_WO -ljasper$$DEBUG_SUFFIX
    vcpkg:win32:LIBS += -ljpeg
    vcpkg:!win32:LIBS += -ljpeg
    vcpkg:unix:!macx:LIBS += -lgomp
    vcpkg:!CONFIG(USE_PDFIUM):LIBS += -llcms2

    win32 {
        DEFINES += LIBRAW_NODLL
        !vcpkg:LIBS += -llibraw
    }

    !vcpkg:macx {
        LIBS += -lraw
    }

    !vcpkg:unix:!macx {
        exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libraw.a) {
            LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libraw.a -fopenmp
        }
        else {
            LIBS += -lraw -fopenmp
        }
    }
}

CONFIG(USE_PDFIUM) {

    SOURCES += src/gfx/gfx_pdfium.cpp

    vcpkg:INCLUDEPATH_EXTERNAL += $$THIRDPARTY_VCPKG_PATH/include/pdfium
    vcpkg:LIBS += -lpdfium -lfreetype$$DEBUG_SUFFIX -ljpeg -lopenjp2 -llcms2

    vcpkg:unix:LIBS += -lpng -lbz2$$DEBUG_SUFFIX
    # is it needed? win has it, mac does not -licuin$$DEBUG_SUFFIX_WO
    vcpkg:win32:LIBS += -lGdi32  -llibpng16$$DEBUG_SUFFIX
    vcpkg:DEFINES += HAVE_PDFIUM

    !vcpkg {
        unix:!macx {
            exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/lib/libpdfium.a) {
                DEFINES += HAVE_PDFIUM
                INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/pdfium
                LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/lib/libpdfium.a
            }
            else:exists(/usr/include/fpdfview.h) {
                DEFINES += HAVE_PDFIUM
                LIBS += -lpdfium
            }
        }
        else {#win/mac
            DEFINES += HAVE_PDFIUM
            INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/pdfium
            LIBS += -lpdfium
        }
    }
}

CONFIG(USE_FFMPEG) {

    unix:!macx {
        vcpkg:LIBS += -lavformat -lavcodec -lavutil -lswscale
        else {
            exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/ffmpeg):exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/lib/libavcodec.a) {
            DEFINES += HAVE_FFMPEG
                INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/ffmpeg
                FFMPEGLIBPATH = $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/lib
            }
            else:exists(/usr/include/ffmpeg-mega) {
                DEFINES += HAVE_FFMPEG
                INCLUDEPATH_EXTERNAL += /usr/include/ffmpeg-mega
                exists(/usr/lib64/libavcodec.a) {
                    FFMPEGLIBPATH = /usr/lib64
                }
                else:exists(/usr/lib32/libavcodec.a) {
                    FFMPEGLIBPATH = /usr/lib32
                }
                else {
                   FFMPEGLIBPATH = /usr/lib
                }
            }
            else:packagesExist(ffmpeg)|packagesExist(libavcodec) {
                DEFINES += HAVE_FFMPEG
                LIBS += -lavcodec -lavformat -lavutil -lswscale -lswresample
            }

            FFMPEGSTATICLIBS = libavformat.a libavcodec.a libavutil.a libswscale.a

            for(ffmpeglib, FFMPEGSTATICLIBS) {
                exists($$FFMPEGLIBPATH/$$ffmpeglib) {
                    LIBS += $$FFMPEGLIBPATH/$$ffmpeglib
                }
            }

            #particular distros requirements
            exists(/usr/lib64/libbz2.so*)|exists(/usr/lib/libbz2.so*) {
                LIBS += -lbz2 #required in fedora ffmpeg/arch compilation
            }

            exists(/usr/lib/liblzma.so*):exists(/etc/arch-release) {
                LIBS += -llzma #required in arch ffmpeg compilation
            }
            CONFIG(FFMPEG_WITH_LZMA) {
                LIBS += -llzma #required in fedora >= 35 ffmpeg compilation
            }
        }
    }
    else { #win/mac
        DEFINES += HAVE_FFMPEG
        vcpkg:INCLUDEPATH_EXTERNAL += $$THIRDPARTY_VCPKG_PATH/include/ffmpeg
        else:INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/ffmpeg
        LIBS += -lavcodec -lavformat -lavutil -lswscale
        vcpkg:macx {
            debug:LIBS += $$THIRDPARTY_VCPKG_PATH/debug/lib/libbz2d.a
            else:LIBS += $$THIRDPARTY_VCPKG_PATH/lib/libbz2.a
        }
        vcpkg:win32 {
            LIBS += -lbz2$$DEBUG_SUFFIX
        }
    }
}

CONFIG(USE_WEBRTC) {

    DEFINES += ENABLE_WEBRTC V8_DEPRECATION_WARNINGS USE_OPENSSL_CERTS=1 NO_TCMALLOC DISABLE_NACL SAFE_BROWSING_DB_REMOTE \
               CHROMIUM_BUILD FIELDTRIAL_TESTING_ENABLED _FILE_OFFSET_BITS=64 __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS \
               _FORTIFY_SOURCE=2 __GNU_SOURCE=1 __compiler_offsetof=__builtin_offsetof NVALGRIND DYNAMIC_ANNOTATIONS_ENABLED=0 \
               WEBRTC_ENABLE_PROTOBUF=1 WEBRTC_INCLUDE_INTERNAL_AUDIO_DEVICE EXPAT_RELATIVE_PATH HAVE_SCTP

    unix {
        DEFINES += WEBRTC_POSIX WEBRTC_LINUX WEBRTC_BUILD_LIBEVENT
    }

    INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/webrtc/include \
                   $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/webrtc/include/webrtc \
                   $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/webrtc/include/third_party/boringssl/src/include \
                   $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/webrtc/include/third_party/libyuv/include \
                   $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/webrtc/include/third_party/abseil-cpp
    !macx {
    exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libwebrtc.a) {
        LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libwebrtc.a -ldl -lX11
    }
    else {
        LIBS += -lwebrtc -ldl -lX11
    }
    }
    else {
        exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libwebrtc.a) {
        LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libwebrtc.a -ldl
        }
        else {
        LIBS += -lwebrtc -ldl
        }
    }
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
        !vcpkg:LIBS +=  -llibcurl -lcares -llibeay32 -lssleay32
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
else:CONFIG += USE_CURL

unix {
SOURCES += src/posix/net.cpp  \
           src/posix/fs.cpp  \
           src/posix/waiter.cpp
}

HEADERS  += include/mega.h \
            include/mega/account.h \
            include/mega/arguments.h \
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
            include/mega/process.h \
            include/mega/pubkeyaction.h \
            include/mega/request.h \
            include/mega/serialize64.h \
            include/mega/nodemanager.h \
            include/mega/setandelement.h \
            include/mega/share.h \
            include/mega/sharenodekeys.h \
            include/mega/sync.h \
            include/mega/syncfilter.h \
            include/mega/heartbeats.h \
            include/mega/transfer.h \
            include/mega/transferslot.h \
            include/mega/treeproc.h \
            include/mega/types.h \
            include/mega/user.h \
            include/mega/useralerts.h \
            include/mega/utils.h \
            include/mega/logging.h \
            include/mega/waiter.h \
            include/mega/proxy.h \
            include/mega/pendingcontactrequest.h \
            include/mega/textchat.h \
            include/mega/crypto/cryptopp.h  \
            include/mega/crypto/sodium.h  \
            include/mega/db/sqlite.h  \
            include/mega/gfx/freeimage.h \
            include/mega/gfx/gfx_pdfium.h \
            include/mega/gfx/external.h \
            include/mega/thread.h \
            include/mega/thread/cppthread.h \
            include/megaapi.h \
            include/megaapi_impl.h \
            include/mega/mega_utf8proc.h \
            include/mega/mega_ccronexpr.h \
            include/mega/mega_evt_tls.h \
            include/mega/mega_evt_queue.h \
            include/mega/thread/posixthread.h \
            include/mega/mega_zxcvbn.h \
            include/mega/mediafileattribute.h \
            include/mega/raid.h \
            include/mega/raidproxy.h \
            include/mega/testhooks.h \
            include/mega/drivenotify.h
CONFIG(USE_MEGAAPI) {
    HEADERS += bindings/qt/QTMegaRequestListener.h \
            bindings/qt/QTMegaTransferListener.h \
            bindings/qt//QTMegaGlobalListener.h \
            bindings/qt/QTMegaListener.h \
            bindings/qt/QTMegaEvent.h
}

win32 {
    HEADERS  += include/mega/win32/megasys.h  \
            include/mega/win32/megafs.h  \
            include/mega/win32/megawaiter.h

    !vcpkg:SOURCES += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/sqlite3.c
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

CONFIG(qt) {
  DEFINES += USE_QT MEGA_QT_LOGGING
}

win32 {
    DEFINES += USE_CPPTHREAD
    SOURCES += src/thread/cppthread.cpp
}
else {
    DEFINES += USE_PTHREAD
    SOURCES += src/thread/posixthread.cpp
    LIBS += -lpthread
}

!CONFIG(nofreeimage) {
    DEFINES += USE_FREEIMAGE
    SOURCES += src/gfx/freeimage.cpp

    vcpkg {
        LIBS += -lFreeImage$$DEBUG_SUFFIX -ljxrglue$$DEBUG_SUFFIX \
            -ljpeg -ltiff$$DEBUG_SUFFIX \
            -lIex-3_1$$UNDERSCORE_DEBUG_SUFFIX -lOpenEXR-3_1$$UNDERSCORE_DEBUG_SUFFIX \
            -lIlmThread-3_1$$UNDERSCORE_DEBUG_SUFFIX -lImath-3_1$$UNDERSCORE_DEBUG_SUFFIX
        win32 {
            LIBS += -llibpng16$$DEBUG_SUFFIX -llibwebpdecoder -llibwebpdemux -llibwebp -llibwebpmux -llibsharpyuv
        }
        else {
            LIBS += -lpng16$$DEBUG_SUFFIX -lwebpdecoder -lwebpdemux -lwebp -lwebpmux -lsharpyuv
        }
        LIBS += -ljpegxr$$DEBUG_SUFFIX -llzma -ljasper$$DEBUG_SUFFIX -lraw_r$$DEBUG_SUFFIX_WO -lopenjp2 -llcms2
    }
    else {
        macx{
            INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/FreeImage/Source
            LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libfreeimage.a
        }
        else {
            exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libfreeimage.so.3) {
                LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libfreeimage.so.3
            }
            else {
                exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libfreeimage.a) {
                    LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libfreeimage.a
                }
                else {
                    LIBS += -lfreeimage
                }
            }
        }
    }
}

DEFINES += USE_SQLITE USE_CRYPTOPP ENABLE_SYNC
INCLUDEPATH += $$MEGASDK_BASE_PATH/include
INCLUDEPATH += $$MEGASDK_BASE_PATH/bindings/qt
vcpkg:INCLUDEPATH_EXTERNAL += $$THIRDPARTY_VCPKG_PATH/include
else:INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include

!release {
    DEFINES += SQLITE_DEBUG DEBUG
}
else {
    DEFINES += NDEBUG
}

vcpkg {
    INCLUDEPATH_EXTERNAL += $$THIRDPARTY_VCPKG_PATH/include/zlib
    INCLUDEPATH_EXTERNAL += $$THIRDPARTY_VCPKG_PATH/include/libsodium

    CONFIG(USE_CURL) {
        !macx:INCLUDEPATH_EXTERNAL += $$THIRDPARTY_VCPKG_PATH/include/openssl
        INCLUDEPATH_EXTERNAL += $$THIRDPARTY_VCPKG_PATH/include/cares
        win32:LIBS +=  -llibcurl$$DASH_DEBUG_SUFFIX -lcares -llibcrypto -llibssl
        else {
            LIBS +=  -lcurl$$DASH_DEBUG_SUFFIX -lcares
            !macx:LIBS += -lcrypto -lssl
        }
    }

    CONFIG(USE_PCRE) {
        INCLUDEPATH_EXTERNAL += $$THIRDPARTY_VCPKG_PATH/include/pcre
        DEFINES += PCRE_STATIC
        LIBS += -lpcre
    }

    release:LIBS += -L"$$THIRDPARTY_VCPKG_PATH/lib"
    debug:LIBS += -L"$$THIRDPARTY_VCPKG_PATH/debug/lib"

    win32:LIBS += -llibsodium -lcryptopp -lzlib$$DEBUG_SUFFIX
    else:LIBS += -lsodium -lcryptopp -lz
    win32:DEFINES += SODIUM_STATIC
    LIBS += -lsqlite3

    # Use the icu lib from vckpg, not the system one.
    win32 {
        LIBS += -licudt$$DEBUG_SUFFIX_WO
        debug:LIBS += -l$$THIRDPARTY_VCPKG_PATH/debug/lib/icuucd -l$$THIRDPARTY_VCPKG_PATH/debug/lib/icuiod
        !debug:LIBS += -l$$THIRDPARTY_VCPKG_PATH/lib/icuuc$$DEBUG_SUFFIX_WO.lib -l$$THIRDPARTY_VCPKG_PATH/lib/icuio$$DEBUG_SUFFIX_WO.lib
        #QMAKE_LFLAGS_WINDOWS += /VERBOSE
    }
    else {
        debug {
            # icu doesn't build debug libs with 'd' suffix, but check anyway
            exists($$THIRDPARTY_VCPKG_PATH/debug/lib/libicuuc$$DEBUG_SUFFIX.a) {
                LIBS += $$THIRDPARTY_VCPKG_PATH/debug/lib/libicuuc$$DEBUG_SUFFIX.a
            }
            else {
                LIBS += $$THIRDPARTY_VCPKG_PATH/debug/lib/libicuuc.a
            }

            exists($$THIRDPARTY_VCPKG_PATH/debug/lib/libicuio$$DEBUG_SUFFIX.a) {
                LIBS += $$THIRDPARTY_VCPKG_PATH/debug/lib/libicuio$$DEBUG_SUFFIX.a
            }
            else {
                LIBS += $$THIRDPARTY_VCPKG_PATH/debug/lib/libicuio.a
            }
        }
        else {
            LIBS += $$THIRDPARTY_VCPKG_PATH/lib/libicuuc.a $$THIRDPARTY_VCPKG_PATH/lib/libicuio.a
        }
    }
}
else {
    # Use system icu if we are not using vcpkg and PDFium is not used. PDFium already includes icu lib.
    !CONFIG(USE_PDFIUM)
    {
        unix:!macx {
            LIBS += -licuuc -licudata
        }
        else {#win/mac
            LIBS += -licu -licudt
        }
    }
}

win32 {
    !vcpkg:INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/zlib
    !vcpkg:INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/libsodium

    CONFIG(USE_CURL) {
        INCLUDEPATH += $$MEGASDK_BASE_PATH/include/mega/wincurl
        !vcpkg:INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/openssl
        !vcpkg:INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/cares
    }
    else {
        INCLUDEPATH += $$MEGASDK_BASE_PATH/include/mega/win32
    }

    !vcpkg:contains(CONFIG, BUILDX64) {
       release {
            LIBS += -L"$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/x64"
        }
        else {
            LIBS += -L"$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/x64d"
        }
    }

    !vcpkg:!contains(CONFIG, BUILDX64) {
        release {
            LIBS += -L"$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/x32"
        }
        else {
            LIBS += -L"$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/x32d"
        }
    }

    CONFIG(USE_PCRE) {
     INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/pcre
     DEFINES += PCRE_STATIC
     LIBS += -lpcre
    }

    LIBS += -lshlwapi -lws2_32 -luser32
    !vcpkg:LIBS += -lsodium -lcryptopp -lzlibstat

    DEFINES += NOMINMAX
    # for inet_ntoa warning
    DEFINES += _WINSOCK_DEPRECATED_NO_WARNINGS
}

unix:!macx {
   INCLUDEPATH += $$MEGASDK_BASE_PATH/include/mega/posix
   LIBS += -lrt

   exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcurl.a) {
    LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcurl.a
   }
   else {
    vcpkg:LIBS += -lcurl$$DASH_DEBUG_SUFFIX
    else:LIBS += -lcurl
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

    exists($$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libsqlite3.a) {
     LIBS +=  $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libsqlite3.a
    }
    else {
     LIBS += -lsqlite3
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
   # recreate source folder tree for object files. Needed to build osx/fs.cpp and posix/fs.cpp,
   # otherwise all obj files are placed into same directory, causing overwrite.
   CONFIG += object_parallel_to_source

   HEADERS += $$MEGASDK_BASE_PATH/include/mega/osx/megafs.h
   SOURCES += $$MEGASDK_BASE_PATH/src/osx/fs.cpp

   INCLUDEPATH += $$MEGASDK_BASE_PATH/include/mega/osx
   INCLUDEPATH += $$MEGASDK_BASE_PATH/include/mega/posix

   OBJECTIVE_SOURCES += $$MEGASDK_BASE_PATH/src/osx/osxutils.mm

   !vcpkg:SOURCES += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/sqlite3.c

   !vcpkg:INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/curl
   !vcpkg:INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/libsodium
   !vcpkg:INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/cares
   !vcpkg:INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/mediainfo
   !vcpkg:INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/zenlib
   !vcpkg:INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/pdfium

   !vcpkg:CONFIG(USE_PCRE) {
    INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/pcre
    DEFINES += PCRE_STATIC
    LIBS += -lpcre
   }

   DEFINES += _DARWIN_FEATURE_64_BIT_INODE

   !vcpkg:LIBS += -L$$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/ $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcares.a $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcurl.a \
                    $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libsodium.a -lcryptopp
   LIBS += -lz

   !vcpkg:CONFIG(USE_OPENSSL) {
    INCLUDEPATH_EXTERNAL += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/include/openssl
    LIBS += $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libssl.a $$MEGASDK_BASE_PATH/bindings/qt/3rdparty/libs/libcrypto.a
   }

   LIBS += -framework SystemConfiguration

   vcpkg:LIBS += -liconv -framework CoreServices -framework CoreFoundation -framework AudioUnit -framework AudioToolbox -framework CoreAudio -framework CoreMedia -framework VideoToolbox -framework ImageIO -framework CoreVideo

    clang {
        COMPILER_VERSION = $$system("$$QMAKE_CXX -dumpversion | cut -d'.' -f1")
        message($$COMPILER_VERSION)
        greaterThan(COMPILER_VERSION, 14) {
            message("Using Xcode 15 or above. Switching to ld_classic linking.")
            LIBS += -Wl,-ld_classic
        }
    }
}

# DriveNotify settings
CONFIG(USE_DRIVE_NOTIFICATIONS) {
    DEFINES += USE_DRIVE_NOTIFICATIONS
    SOURCES += src/drivenotify.cpp

    win32 {
        # Allegedly not supported by non-msvc compilers.
        HEADERS += include/mega/win32/drivenotifywin.h
        SOURCES += src/win32/drivenotifywin.cpp
        LIBS += -lwbemuuid -lole32 -loleaut32
    }
    unix:!macx {
        HEADERS += include/mega/posix/drivenotifyposix.h
        SOURCES += src/posix/drivenotifyposix.cpp
        LIBS += -ludev
    }
    macx {
        HEADERS += include/mega/osx/drivenotifyosx.h
        SOURCES += src/osx/drivenotifyosx.cpp
        LIBS += -framework DiskArbitration -framework CoreFoundation
    }
}

# Add include paths as system libs to avoid warnings from external libraries.
unix {
    for(incpath, INCLUDEPATH_EXTERNAL) {
        QMAKE_CXXFLAGS += -isystem "$$incpath"
    }
} else {
    INCLUDEPATH += $$INCLUDEPATH_EXTERNAL
}

# So we don't clobber other object files.
CONFIG += object_parallel_to_source

# Required by all FUSE backends.
FUSE_COMMON_SRC = $$MEGASDK_BASE_PATH/src/fuse/common
FUSE_COMMON_INC = $$MEGASDK_BASE_PATH/include/mega/fuse/common

HEADERS += \
    $$FUSE_COMMON_INC/activity_monitor.h \
    $$FUSE_COMMON_INC/activity_monitor_forward.h \
    $$FUSE_COMMON_INC/any_lock.h \
    $$FUSE_COMMON_INC/any_lock_forward.h \
    $$FUSE_COMMON_INC/any_lock_set.h \
    $$FUSE_COMMON_INC/any_lock_set_forward.h \
    $$FUSE_COMMON_INC/bind_handle.h \
    $$FUSE_COMMON_INC/bind_handle_forward.h \
    $$FUSE_COMMON_INC/client_adapter.h \
    $$FUSE_COMMON_INC/client_callbacks.h \
    $$FUSE_COMMON_INC/client_forward.h \
    $$FUSE_COMMON_INC/client.h \
    $$FUSE_COMMON_INC/database.h \
    $$FUSE_COMMON_INC/database_forward.h \
    $$FUSE_COMMON_INC/error_or.h \
    $$FUSE_COMMON_INC/error_or_forward.h \
    $$FUSE_COMMON_INC/inode_cache_flags.h \
    $$FUSE_COMMON_INC/inode_cache_flags_forward.h \
    $$FUSE_COMMON_INC/inode_id.h \
    $$FUSE_COMMON_INC/inode_id_forward.h \
    $$FUSE_COMMON_INC/inode_info.h \
    $$FUSE_COMMON_INC/inode_info_forward.h \
    $$FUSE_COMMON_INC/lock.h \
    $$FUSE_COMMON_INC/lock_forward.h \
    $$FUSE_COMMON_INC/lockable.h \
    $$FUSE_COMMON_INC/lockable_forward.h \
    $$FUSE_COMMON_INC/log_level.h \
    $$FUSE_COMMON_INC/log_level_forward.h \
    $$FUSE_COMMON_INC/logger.h \
    $$FUSE_COMMON_INC/logger_forward.h \
    $$FUSE_COMMON_INC/logging.h \
    $$FUSE_COMMON_INC/mount_event_forward.h \
    $$FUSE_COMMON_INC/mount_event_type_forward.h \
    $$FUSE_COMMON_INC/mount_event_type.h \
    $$FUSE_COMMON_INC/mount_event.h \
    $$FUSE_COMMON_INC/mount_flags_forward.h \
    $$FUSE_COMMON_INC/mount_flags.h \
    $$FUSE_COMMON_INC/mount_info_forward.h \
    $$FUSE_COMMON_INC/mount_info.h \
    $$FUSE_COMMON_INC/mount_inode_id_forward.h \
    $$FUSE_COMMON_INC/mount_inode_id.h \
    $$FUSE_COMMON_INC/mount_result_forward.h \
    $$FUSE_COMMON_INC/mount_result.h \
    $$FUSE_COMMON_INC/node_event.h \
    $$FUSE_COMMON_INC/node_event_forward.h \
    $$FUSE_COMMON_INC/node_event_observer.h \
    $$FUSE_COMMON_INC/node_event_observer_forward.h \
    $$FUSE_COMMON_INC/node_event_queue.h \
    $$FUSE_COMMON_INC/node_event_queue_forward.h \
    $$FUSE_COMMON_INC/node_event_type.h \
    $$FUSE_COMMON_INC/node_event_type_forward.h \
    $$FUSE_COMMON_INC/node_info.h \
    $$FUSE_COMMON_INC/node_info_forward.h \
    $$FUSE_COMMON_INC/normalized_path_forward.h \
    $$FUSE_COMMON_INC/normalized_path.h \
    $$FUSE_COMMON_INC/pending_callbacks.h \
    $$FUSE_COMMON_INC/query_forward.h \
    $$FUSE_COMMON_INC/query.h \
    $$FUSE_COMMON_INC/scoped_query_forward.h \
    $$FUSE_COMMON_INC/scoped_query.h \
    $$FUSE_COMMON_INC/service_callbacks.h \
    $$FUSE_COMMON_INC/service_context_forward.h \
    $$FUSE_COMMON_INC/service_context.h \
    $$FUSE_COMMON_INC/service_flags_forward.h \
    $$FUSE_COMMON_INC/service_flags.h \
    $$FUSE_COMMON_INC/service_forward.h \
    $$FUSE_COMMON_INC/service.h \
    $$FUSE_COMMON_INC/shared_mutex_forward.h \
    $$FUSE_COMMON_INC/shared_mutex.h \
    $$FUSE_COMMON_INC/task_executor_flags_forward.h \
    $$FUSE_COMMON_INC/task_executor_flags.h \
    $$FUSE_COMMON_INC/task_executor_forward.h \
    $$FUSE_COMMON_INC/task_executor.h \
    $$FUSE_COMMON_INC/task_queue_forward.h \
    $$FUSE_COMMON_INC/task_queue.h \
    $$FUSE_COMMON_INC/transaction_forward.h \
    $$FUSE_COMMON_INC/transaction.h \
    $$FUSE_COMMON_INC/upload_forward.h \
    $$FUSE_COMMON_INC/upload.h \
    $$FUSE_COMMON_INC/utility.h

SOURCES += \
    $$FUSE_COMMON_SRC/activity_monitor.cpp \
    $$FUSE_COMMON_SRC/any_lock_set.cpp \
    $$FUSE_COMMON_SRC/bind_handle.cpp \
    $$FUSE_COMMON_SRC/client.cpp \
    $$FUSE_COMMON_SRC/client_adapter.cpp \
    $$FUSE_COMMON_SRC/client_adapter_with_sync.cpp \
    $$FUSE_COMMON_SRC/database.cpp \
    $$FUSE_COMMON_SRC/inode_id.cpp \
    $$FUSE_COMMON_SRC/inode_info.cpp \
    $$FUSE_COMMON_SRC/log_level.cpp \
    $$FUSE_COMMON_SRC/logger.cpp \
    $$FUSE_COMMON_SRC/mount_event.cpp \
    $$FUSE_COMMON_SRC/mount_event_type.cpp \
    $$FUSE_COMMON_SRC/mount_flags.cpp \
    $$FUSE_COMMON_SRC/mount_info.cpp \
    $$FUSE_COMMON_SRC/mount_inode_id.cpp \
    $$FUSE_COMMON_SRC/mount_result.cpp \
    $$FUSE_COMMON_SRC/node_event_type.cpp \
    $$FUSE_COMMON_SRC/normalized_path.cpp \
    $$FUSE_COMMON_SRC/pending_callbacks.cpp \
    $$FUSE_COMMON_SRC/query.cpp \
    $$FUSE_COMMON_SRC/scoped_query.cpp \
    $$FUSE_COMMON_SRC/service.cpp \
    $$FUSE_COMMON_SRC/service_context.cpp \
    $$FUSE_COMMON_SRC/shared_mutex.cpp \
    $$FUSE_COMMON_SRC/task_executor.cpp \
    $$FUSE_COMMON_SRC/task_queue.cpp \
    $$FUSE_COMMON_SRC/transaction.cpp \
    $$FUSE_COMMON_SRC/utility.cpp

# Required by all concrete backends.
CONFIG(WITH_FUSE) {
    HEADERS += \
        $$FUSE_COMMON_INC/badge_forward.h \
        $$FUSE_COMMON_INC/badge.h \
        $$FUSE_COMMON_INC/constants.h \
        $$FUSE_COMMON_INC/database_builder.h \
        $$FUSE_COMMON_INC/date_time_forward.h \
        $$FUSE_COMMON_INC/date_time.h \
        $$FUSE_COMMON_INC/directory_inode_forward.h \
        $$FUSE_COMMON_INC/directory_inode_results.h \
        $$FUSE_COMMON_INC/directory_inode.h \
        $$FUSE_COMMON_INC/error_or.h \
        $$FUSE_COMMON_INC/error_or_forward.h \
        $$FUSE_COMMON_INC/file_cache.h \
        $$FUSE_COMMON_INC/file_cache_forward.h \
        $$FUSE_COMMON_INC/file_extension_db.h \
        $$FUSE_COMMON_INC/file_extension_db_forward.h \
        $$FUSE_COMMON_INC/file_info.h \
        $$FUSE_COMMON_INC/file_info_forward.h \
        $$FUSE_COMMON_INC/file_inode.h \
        $$FUSE_COMMON_INC/file_inode_forward.h \
        $$FUSE_COMMON_INC/file_io_context.h \
        $$FUSE_COMMON_INC/file_io_context_forward.h \
        $$FUSE_COMMON_INC/inode_cache.h \
        $$FUSE_COMMON_INC/inode_cache_forward.h \
        $$FUSE_COMMON_INC/inode_db.h \
        $$FUSE_COMMON_INC/inode_db_forward.h \
        $$FUSE_COMMON_INC/inode_forward.h \
        $$FUSE_COMMON_INC/inode.h \
        $$FUSE_COMMON_INC/mount_db_forward.h \
        $$FUSE_COMMON_INC/mount_db.h \
        $$FUSE_COMMON_INC/mount_forward.h \
        $$FUSE_COMMON_INC/mount.h \
        $$FUSE_COMMON_INC/path_adapter_forward.h \
        $$FUSE_COMMON_INC/path_adapter.h \
        $$FUSE_COMMON_INC/ref_forward.h \
        $$FUSE_COMMON_INC/ref.h \
        $$FUSE_COMMON_INC/tags.h

    SOURCES += \
        $$FUSE_COMMON_SRC/database_builder.cpp \
        $$FUSE_COMMON_SRC/date_time.cpp \
        $$FUSE_COMMON_SRC/directory_inode.cpp \
        $$FUSE_COMMON_SRC/file_cache.cpp \
        $$FUSE_COMMON_SRC/file_extension_db.cpp \
        $$FUSE_COMMON_SRC/file_info.cpp \
        $$FUSE_COMMON_SRC/file_inode.cpp \
        $$FUSE_COMMON_SRC/file_io_context.cpp \
        $$FUSE_COMMON_SRC/inode.cpp \
        $$FUSE_COMMON_SRC/inode_cache.cpp \
        $$FUSE_COMMON_SRC/inode_db.cpp \
        $$FUSE_COMMON_SRC/mount_db.cpp \
        $$FUSE_COMMON_SRC/mount.cpp

    FUSE_SUPPORTED_SRC = $$MEGASDK_BASE_PATH/src/fuse/supported
    FUSE_SUPPORTED_INC = $$MEGASDK_BASE_PATH/src/fuse/supported/mega/fuse

    INCLUDEPATH += $$FUSE_SUPPORTED_SRC

    HEADERS += \
        $$FUSE_SUPPORTED_INC/platform/context_forward.h \
        $$FUSE_SUPPORTED_INC/platform/context.h \
        $$FUSE_SUPPORTED_INC/platform/directory_context_forward.h \
        $$FUSE_SUPPORTED_INC/platform/file_context_forward.h \
        $$FUSE_SUPPORTED_INC/platform/file_context.h \
        $$FUSE_SUPPORTED_INC/platform/mount_db_forward.h \
        $$FUSE_SUPPORTED_INC/platform/mount_forward.h \
        $$FUSE_SUPPORTED_INC/platform/path_adapter_forward.h \
        $$FUSE_SUPPORTED_INC/platform/path_adapter.h \
        $$FUSE_SUPPORTED_INC/platform/service_context_forward.h \
        $$FUSE_SUPPORTED_INC/platform/service_context.h \
        $$FUSE_SUPPORTED_INC/platform/unmounter.h

    SOURCES += \
        $$FUSE_SUPPORTED_SRC/context.cpp \
        $$FUSE_SUPPORTED_SRC/file_context.cpp \
        $$FUSE_SUPPORTED_SRC/service_context.cpp \
        $$FUSE_SUPPORTED_SRC/service.cpp \
        $$FUSE_SUPPORTED_SRC/unmounter.cpp

    # Sources required by all POSIX backends.
    unix {
        FUSE_POSIX_SRC = $$FUSE_SUPPORTED_SRC/posix
        FUSE_POSIX_INC = $$FUSE_POSIX_SRC/mega/fuse

        INCLUDEPATH += $$FUSE_POSIX_SRC

        HEADERS += \
            $$FUSE_POSIX_INC/platform/constants.h \
            $$FUSE_POSIX_INC/platform/directory_context.h \
            $$FUSE_POSIX_INC/platform/file_descriptor.h \
            $$FUSE_POSIX_INC/platform/file_descriptor_forward.h \
            $$FUSE_POSIX_INC/platform/inode_invalidator.h \
            $$FUSE_POSIX_INC/platform/library.h \
            $$FUSE_POSIX_INC/platform/mount.h \
            $$FUSE_POSIX_INC/platform/mount_db.h \
            $$FUSE_POSIX_INC/platform/path_adapter.h \
            $$FUSE_POSIX_INC/platform/platform.h \
            $$FUSE_POSIX_INC/platform/process.h \
            $$FUSE_POSIX_INC/platform/process_forward.h \
            $$FUSE_POSIX_INC/platform/request.h \
            $$FUSE_POSIX_INC/platform/request_forward.h \
            $$FUSE_POSIX_INC/platform/session.h \
            $$FUSE_POSIX_INC/platform/session_forward.h \
            $$FUSE_POSIX_INC/platform/signal.h \
            $$FUSE_POSIX_INC/platform/utility.h

        SOURCES += \
            $$FUSE_POSIX_SRC/directory_context.cpp \
            $$FUSE_POSIX_SRC/constants.cpp \
            $$FUSE_POSIX_SRC/file_descriptor.cpp \
            $$FUSE_POSIX_SRC/inode_invalidator.cpp \
            $$FUSE_POSIX_SRC/mount.cpp \
            $$FUSE_POSIX_SRC/mount_db.cpp \
            $$FUSE_POSIX_SRC/request.cpp \
            $$FUSE_POSIX_SRC/service.cpp \
            $$FUSE_POSIX_SRC/session.cpp \
            $$FUSE_POSIX_SRC/signal.cpp \
            $$FUSE_POSIX_SRC/unmounter.cpp \
            $$FUSE_POSIX_SRC/utility.cpp

        # User's told us where FUSE should live.
        !isEmpty(FUSE_PREFIX) {
            # User hasn't told us where to find FUSE's headers.
            isEmpty(FUSE_INCLUDE_DIR) {
                FUSE_INCLUDE_DIR=$$FUSE_PREFIX/include
            }

            # User hasn't told us where to find FUSE's libraries.
            isEmpty(FUSE_LIB_DIR) {
                FUSE_LIB_DIR=$$FUSE_PREFIX/lib
            }
        }

        # Make sure header and library paths are sane.
        isEmpty(FUSE_INCLUDE_DIR) {
            error("You must specify either FUSE_INCLUDE_DIR or FUSE_PREFIX")
        }

        isEmpty(FUSE_LIB_DIR) {
            error("You must specify either FUSE_LIB_DIR or FUSE_PREFIX")
        }

        # Make sure FUSE's headers are present.
        !exists($$FUSE_INCLUDE_DIR/fuse/fuse_lowlevel.h) {
            error("Couldn't find fuse/fuse_lowlevel.h under $$FUSE_INCLUDE_DIR")
        }

        DEFINES += _FILE_OFFSET_BITS=64
        INCLUDEPATH += $$FUSE_INCLUDE_DIR

        # Sources required for Linux.
        !macx {
            # Make sure the FUSE library is present.
            !exists($$FUSE_LIB_DIR/libfuse.a) {
                error("Couldn't find libfuse.a under $$FUSE_LIB_DIR")
            }

            LIBS += $$FUSE_LIB_DIR/libfuse.a

            FUSE_UNIX_SRC = $$FUSE_POSIX_SRC/linux
            FUSE_UNIX_INC = $$FUSE_UNIX_SRC/mega/fuse
        } # !macx

        macx {
            # Make sure the FUSE library is present.
            !exists($$FUSE_LIB_DIR/libfuse.dylib) {
                error("Couldn't find libfuse.dylib under $$FUSE_LIB_DIR")
            }

            LIBS += $$FUSE_LIB_DIR/libfuse.dylib

            FUSE_UNIX_SRC = $$FUSE_POSIX_SRC/darwin
            FUSE_UNIX_INC = $$FUSE_UNIX_SRC/mega/fuse
        } # macx

        isEmpty(FUSE_UNIX_SRC):error("Unsupported UNIX implementation")

        INCLUDEPATH += $$FUSE_UNIX_SRC

        HEADERS += \
            $$FUSE_UNIX_INC/platform/date-time.h \
            $$FUSE_UNIX_INC/platform/platform.h

        SOURCES += \
            $$FUSE_UNIX_SRC/utility.cpp
    } # unix

    # Sources required by WinFSP backend.
    win32 {
        FUSE_WINDOWS_SRC = $$FUSE_SUPPORTED_SRC/windows
        FUSE_WINDOWS_INC = $$FUSE_WINDOWS_SRC/fuse

        INCLUDEPATH += $$FUSE_WINDOWS_SRC

        HEADERS += \
            $$FUSE_WINDOWS_INC/platform/constants.h \
            $$FUSE_WINDOWS_INC/platform/date_time.h \
            $$FUSE_WINDOWS_INC/platform/directory_context.h \
            $$FUSE_WINDOWS_INC/platform/dispatcher_forward.h \
            $$FUSE_WINDOWS_INC/platform/dispatcher.h \
            $$FUSE_WINDOWS_INC/platform/handle_forward.h \
            $$FUSE_WINDOWS_INC/platform/handle.h \
            $$FUSE_WINDOWS_INC/platform/library.h \
            $$FUSE_WINDOWS_INC/platform/local_pointer.h \
            $$FUSE_WINDOWS_INC/platform/mount_db.h \
            $$FUSE_WINDOWS_INC/platform/mount.h \
            $$FUSE_WINDOWS_INC/platform/path_adapter.h \
            $$FUSE_WINDOWS_INC/platform/platform.h \
            $$FUSE_WINDOWS_INC/platform/security_descriptor_forward.h \
            $$FUSE_WINDOWS_INC/platform/security_descriptor.h \
            $$FUSE_WINDOWS_INC/platform/security_identifier_forward.h \
            $$FUSE_WINDOWS_INC/platform/security_identifier.h \
            $$FUSE_WINDOWS_INC/platform/windows.h \
            $$FUSE_WINDOWS_INC/utility.h

        SOURCES += \
            $$FUSE_WINDOWS_SRC/constants.cpp \
            $$FUSE_WINDOWS_SRC/directory_context.cpp \
            $$FUSE_WINDOWS_SRC/dispatcher.cpp \
            $$FUSE_WINDOWS_SRC/local_pointer.cpp \
            $$FUSE_WINDOWS_SRC/mount.cpp \
            $$FUSE_WINDOWS_SRC/mount_db.cpp \
            $$FUSE_WINDOWS_SRC/security_descriptor.cpp \
            $$FUSE_WINDOWS_SRC/security_identifier.cpp \
            $$FUSE_WINDOWS_SRC/service.cpp \
            $$FUSE_WINDOWS_SRC/unmounter.cpp \
            $$FUSE_WINDOWS_SRC/utility.cpp

        !isEmpty(WINFSP_PREFIX) {
            # User hasn't specified where to find WinFSP's headers.
            isEmpty(WINFSP_INCLUDE_DIR) {
                WINFSP_INCLUDE_DIR=$$WINFSP_PREFIX\inc
            }

            # User hasn't specified where to find WinFSP's libraries.
            isEmpty(WINFSP_LIB_DIR) {
                WINFSP_LIB_DIR=$$WINFSP_PREFIX\lib
            }
        }

        # Make sure header and library paths are sane.
        isEmpty(WINFSP_INCLUDE_DIR) {
            error("You must specify either WINFSP_INCLUDE_DIR or WINFSP_PREFIX")
        }

        isEmpty(WINFSP_LIB_DIR) {
            error("You must specify either WINFSP_LIB_DIR or WINFSP_PREFIX")
        }

        # Make sure WinFSP's headers are present.
        !exists($$WINFSP_INCLUDE_DIR/winfsp/winfsp.h) {
            error("Couldn't find winfsp/winfsp.h under $$WINFSP_INCLUDE_DIR")
        }

        # Necessary so we don't refine lots of Win32 macros.
        DEFINES += WIN32_LEAN_AND_MEAN

        # Let the SDK know where it can find WinFSP's headers.
        INCLUDEPATH += "$$WINFSP_INCLUDE_DIR"

        # Assume we're building a 64bit binary.
        WINFSP_LIB = winfsp-x64.lib

        # Actually building a 32bit binary.
        contains(QT_ARCH, i386):WINFSP_LIB = winfsp-x86.lib

        # Make sure the WinFSP library is present.
        !exists($$WINFSP_LIB_DIR/$$WINFSP_LIB) {
            error("Couldn't find $$WINFSP_LIB under $$WINFSP_LIB_DIR")
        }

        # Delay load the WinFSP DLL.
        QMAKE_LFLAGS += /DELAYLOAD:$$replace(WINFSP_LIB, lib, dll)

        LIBS += delayimp.lib

        # Make sure the SDK is linked against WinFSP.
        LIBS += "$$WINFSP_LIB_DIR/$$WINFSP_LIB"
    } # win32
} # WITH_FUSE

# Required by dummy backend.
!CONFIG(WITH_FUSE) {
    FUSE_UNSUPPORTED_SRC = $$MEGASDK_BASE_PATH/src/fuse/unsupported
    FUSE_UNSUPPORTED_INC = $$MEGASDK_BASE_PATH/src/fuse/unsupported/mega/fuse

    INCLUDEPATH += $$FUSE_UNSUPPORTED_SRC

    HEADERS += \
        $$FUSE_UNSUPPORTED_INC/platform/service_context.h

    SOURCES += \
        $$FUSE_UNSUPPORTED_SRC/service_context.cpp \
        $$FUSE_UNSUPPORTED_SRC/service.cpp
} # !WITH_FUSE

