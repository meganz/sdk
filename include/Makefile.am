libmegaincludedir=$(includedir)

nobase_libmegainclude_HEADERS = \
	mega.h \
	mega/account.h \
	mega/arguments.h \
	mega/attrmap.h \
	mega/backofftimer.h \
	mega/base64.h \
	mega/config.h \
	mega/console.h \
	mega/command.h \
	mega/db.h \
	mega/drivenotify.h \
	mega/gfx.h \
	mega/fileattributefetch.h \
	mega/filefingerprint.h \
	mega/file.h \
	mega/filesystem.h \
	mega/http.h \
	mega/json.h \
	mega/megaapp.h \
	mega/megaclient.h \
	mega/node.h \
	mega/process.h \
	mega/pubkeyaction.h \
	mega/request.h \
	mega/serialize64.h \
	mega/share.h \
	mega/sharenodekeys.h \
	mega/sync.h \
	mega/transfer.h \
	mega/transferslot.h \
	mega/transferstats.h \
	mega/treeproc.h \
	mega/types.h \
	mega/user.h \
	mega/utils.h \
	mega/useralerts.h \
	mega/logging.h \
	mega/waiter.h \
	mega/proxy.h \
	mega/pendingcontactrequest.h \
	mega/version.h \
	mega/crypto/cryptopp.h \
	mega/crypto/sodium.h \
	mega/db/sqlite.h \
	mega/thread.h \
	mega/thread/cppthread.h \
	mega/thread/posixthread.h \
	mega/gfx/external.h \
	mega/mega_utf8proc.h \
	mega/mega_ccronexpr.h \
	mega/mega_evt_tls.h \
	mega/mega_evt_queue.h \
	mega/mega_zxcvbn.h \
	mega/mega_dict-src.h \
	mega/gfx/gfx_pdfium.h \
	mega/gfx/freeimage.h \
	mega/gfx/GfxProcCG.h \
	mega/raid.h \
	mega/mediafileattribute.h \
	mega/raidproxy.h

if BUILD_MEGAAPI
nobase_libmegainclude_HEADERS += megaapi.h megaapi_impl.h
endif

if USE_LIBUV
nobase_libmegainclude_HEADERS += mega/mega_http_parser.h
endif

if ANDROID
nobase_libmegainclude_HEADERS += \
	mega/config-android.h \
	mega/mega_glob.h
endif

if WIN32
nobase_libmegainclude_HEADERS += \
	mega/win32/megasys.h \
	mega/win32/megafs.h \
	mega/win32/meganet.h \
	mega/win32/megawaiter.h \
	mega/win32/megaconsole.h \
	mega/win32/megaconsolewaiter.h
else
nobase_libmegainclude_HEADERS += \
	mega/posix/megasys.h \
	mega/posix/megafs.h \
	mega/posix/meganet.h \
	mega/posix/megawaiter.h \
	mega/posix/megaconsole.h \
	mega/posix/megaconsolewaiter.h
endif

if DARWIN
if !USE_IOS
nobase_libmegainclude_HEADERS += mega/osx/megafs.h
nobase_libmegainclude_HEADERS += mega/osx/osxutils.h
endif
endif

#noinst_HEADERS = config.h
