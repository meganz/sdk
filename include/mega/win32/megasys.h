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

// platform dependent constants
#if defined(__ANDROID__) && !defined(HAVE_SDK_CONFIG_H)
#include "mega/config-android.h"
#else
#include "mega/config.h"
#endif

// FIXME: move to autoconf
#ifndef __STDC_FORMAT_MACROS
  #define __STDC_FORMAT_MACROS
#endif

#include <algorithm>
#include <array>
#include <assert.h>
#include <deque>
#include <errno.h>
#include <functional>
#include <inttypes.h>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <math.h>
#include <memory.h>
#include <queue>
#include <set>
#include <sstream>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string> // the MEGA SDK assumes writable, contiguous string::data()
#include <time.h>

#if defined(USE_PTHREAD) && defined (__MINGW32__)
#include <sys/time.h>		
#endif

#include <specstrings.h>

#pragma push_macro("NOMINMAX")
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#pragma pop_macro("NOMINMAX")

#ifdef __MINGW32__
 //#include <wincrypt.h> // x509 define clashes with webrtc
#endif
 //#include <wincrypt.h> // x509 define clashes with webrtc
 #include <shlwapi.h>

#include <shellapi.h>

#define atoll _atoi64
#define strncasecmp _strnicmp
#define strtoull _strtoui64

#ifndef strcasecmp
#define strcasecmp _stricmp
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
  #define _CRT_SECURE_NO_WARNINGS
#endif

#include <conio.h>
#include <stdexcept>

#endif
