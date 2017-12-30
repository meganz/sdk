/**
 * @file mega/win32/megasys.h
 * @brief Mega SDK platform-specific includes (Win32)
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

#ifndef MEGA_WIN32_OS_H
#define MEGA_WIN32_OS_H 1

#ifdef HAVE_CONFIG_H
// platform dependent constants
#ifdef __ANDROID__
#include "mega/config-android.h"
#else
#include "mega/config.h"
#endif
#endif

// FIXME: move to autoconf
#ifndef __STDC_FORMAT_MACROS
  #define __STDC_FORMAT_MACROS
#endif

#ifdef WINDOWS_PHONE
#define __STDC_LIMIT_MACROS
#endif

// (inttypes.h is not present in Microsoft Visual Studio < 2015)
#if (defined (MSC_VER) && (_MSC_VER < 1900)) && !defined(HAVE_INTTYPES_H)
  #define PRIu32 "I32u"
  #define PRIu64 "I64u"
  #define PRId64 "I64d"
  #define PRIi64 "I64i"
#else
  #include <inttypes.h>
#endif

#include <iostream>
#include <algorithm>
#include <string>   // the MEGA SDK assumes writable, contiguous string::data()
#include <sstream>
#include <map>
#include <deque>
#include <set>
#include <iterator>
#include <queue>
#include <list>
#include <functional>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <memory.h>
#include <time.h>

#if defined(USE_PTHREAD) && defined (__MINGW32__)
#include <sys/time.h>		
#endif

#include <specstrings.h>
#include <winsock2.h>
#include <windows.h>

#ifndef WINDOWS_PHONE
 #include <wincrypt.h>
 #include <winhttp.h>
 #include <shlwapi.h>
#endif

#include <shellapi.h>

#define atoll _atoi64
#define snprintf mega_snprintf
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define strtoull _strtoui64

#ifndef _CRT_SECURE_NO_WARNINGS
  #define _CRT_SECURE_NO_WARNINGS
#endif

#include <conio.h>

#endif
