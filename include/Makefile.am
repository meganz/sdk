include_libmegadir = $(includedir)/mega

libmega_headers = megaclient.h \
        crypto/cryptopp.h \
        db/sqlite.h \
        db/bdb.h


# win32 headers
if PLATFORM_WIN32
libmega_headers += \
        win32/fs.h \
        win32/wait.h \
        win32/net.h \
        win32/console.h

# posix headers
else
libmega_headers += \
        posix/fs.h \
        posix/wait.h \
        posix/net.h \
        posix/console.h \
        posix/megaposix.h

endif

noinst_HEADERS = megaclient_private.h

nobase_include_HEADERS = $(libmega_headers)
