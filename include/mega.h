/**
 * @file mega.h
 * @brief Main header file for inclusion by client software.
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef MEGA_H
#define MEGA_H 1

#ifndef MEGA_SDK
#define MEGA_SDK
#endif

// version
#include "mega/version.h"

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wunqualified-std-cast-call"
#endif

// project types
#include "mega/types.h"

// project includes
#include "mega/account.h"
#include "mega/http.h"
#include "mega/proxy.h"
#include "mega/attrmap.h"
#include "mega/backofftimer.h"
#include "mega/base64.h"
#include "mega/command.h"
#include "mega/console.h"
#include "mega/fileattributefetch.h"
#include "mega/filefingerprint.h"
#include "mega/file.h"
#include "mega/filesystem.h"
#include "mega/db.h"
#include "mega/json.h"
#include "mega/pubkeyaction.h"
#include "mega/request.h"
#include "mega/serialize64.h"
#include "mega/share.h"
#include "mega/sharenodekeys.h"
#include "mega/treeproc.h"
#include "mega/user.h"
#include "mega/pendingcontactrequest.h"
#include "mega/utils.h"
#include "mega/logging.h"
#include "mega/waiter.h"

#include "mega/node.h"
#include "mega/sync.h"
#include "mega/transfer.h"
#include "mega/transferslot.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"

// target-specific headers
#include "mega/thread/posixthread.h"
#include "mega/thread/cppthread.h"

#ifdef USE_IOS
#include "mega/posix/megawaiter.h"
#include "mega/posix/meganet.h"
#include "mega/osx/megafs.h"
#include "mega/posix/megaconsole.h"
#include "mega/posix/megaconsolewaiter.h"
#else
#include "megawaiter.h"
#include "meganet.h"
#include "megafs.h"
#include "megaconsole.h"
#include "megaconsolewaiter.h"
#endif

#include "mega/db/sqlite.h"

#include "mega/gfx/freeimage.h"
#include "mega/gfx/GfxProcCG.h"


#if defined(REQUIRE_HAVE_FFMPEG) && !defined(HAVE_FFMPEG)
#error compilation with HAVE_FFMPEG is required
#endif
#if defined(REQUIRE_HAVE_LIBUV) && !defined(HAVE_LIBUV)
#error compilation with HAVE_LIBUV is required
#endif
#if defined(REQUIRE_HAVE_LIBRAW) && !defined(HAVE_LIBRAW)
#error compilation with HAVE_LIBRAW is required
#endif
#if defined(REQUIRE_HAVE_PDFIUM) && !defined(HAVE_PDFIUM)
#error compilation with HAVE_PDFIUM is required
#endif
#if defined(REQUIRE_ENABLE_CHAT) && !defined(ENABLE_CHAT)
#error compilation with ENABLE_CHAT is required
#endif
#if defined(REQUIRE_ENABLE_BACKUPS) && !defined(ENABLE_BACKUPS)
#error compilation with ENABLE_BACKUPS is required
#endif
#if defined(REQUIRE_ENABLE_WEBRTC) && !defined(ENABLE_WEBRTC)
#error compilation with ENABLE_WEBRTC is required
#endif
#if defined(REQUIRE_ENABLE_EVT_TLS) && !defined(ENABLE_EVT_TLS)
#error compilation with ENABLE_EVT_TLS is required
#endif
#if defined(REQUIRE_USE_MEDIAINFO) && !defined(USE_MEDIAINFO)
#error compilation with USE_MEDIAINFO is required
#endif
#if defined(REQUIRE_USE_PCRE) && !defined(USE_PCRE)
#error compilation with USE_PCRE is required
#endif

#endif
