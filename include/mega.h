/**
 * @file mega.h
 * @brief Main header file for inclusion by client software.
 * 
 * (c) 2013 by Mega Limited, Wellsford, New Zealand
 * 
 * This file is part of the MEGA SDK - Client Access Engine.
 * 
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 * 
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * Simplified (2-clause) BSD License for more details.
 *
 * You should have received a copy of the Simplified (2-clause) BSD
 * License along with this program.
 */

#ifndef MEGA_H
#define MEGA_H 1

#ifndef MEGA_SDK
#define MEGA_SDK
#endif

/// Major version for API.
#define MEGASDK_VERSION_MAJOR 0
/// Minor version for API.
#define MEGASDK_VERSION_MINOR 9
/// Patch level for API.
#define MEGASDK_VERSION_PATCH 3

/// Macro to generate compound version of API.
#define MEGASDK_MAKE_VERSION(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))
/// Numeric compound version for API.
#define MEGASDK_VERSION \
    MEGASDK_MAKE_VERSION(MEGASDK_VERSION_MAJOR, \
                         MEGASDK_VERSION_MINOR, \
                         MEGASDK_VERSION_PATCH)

// Windows specific includes
#ifdef _WIN32

#include <windows.h>
#include <winsock2.h>
#define atoll _atoi64
#define snprintf _snprintf
#define _CRT_SECURE_NO_WARNINGS
#define my_socket SOCKET
typedef int my_socket;

using namespace std;
#include <iostream>
#include <algorithm>
#include <string>
#include <sstream>
#include <map>
#include <set>
#include <iterator>
#include <queue>
#include <list>

// Linux specific includes
#else

// XXX: posix
//#define _POSIX_SOURCE
#define _LARGE_FILES
//#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64
#define __DARWIN_C_LEVEL 199506L

#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <glob.h>
#include <dirent.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <memory.h>
#include <time.h>

using namespace std;
#include <iostream>
#include <algorithm>
#include <string>
#include <sstream>
#include <map>
#include <set>
#include <iterator>
#include <queue>
#include <list>

// FIXME: #define PRI*64 if missing
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <sys/un.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <termios.h>

#ifndef __MACH__
#include <endian.h>
#endif

#include <sys/sendfile.h>
#include <sys/inotify.h>
#include <sys/select.h>

#include <curl/curl.h>
#endif // end of Linux specific includes


typedef int64_t m_off_t;

// monotonously increasing time in deciseconds
typedef uint32_t dstime;


#ifdef __MACH__

// FIXME: revisit OS X support
#include <machine/endian.h>
#include <strings.h>
#include <sys/time.h>
#define CLOCK_MONOTONIC 0
int clock_gettime(int, struct timespec* t)
{
    struct timeval now;
    int rv = gettimeofday(&now,NULL);
    if (rv) return rv;
    t->tv_sec  = now.tv_sec;
    t->tv_nsec = now.tv_usec*1000;
    return 0;
}

#endif


#include "crypto/cryptopp.h"
#include "megaclient.h"

// Windows specific includes
#ifdef _WIN32
#include "win32/net.h"
#include "win32/fs.h"
#include "win32/console.h"
#include "win32/net.h"

// Linux specific includes
#else
#include "posix/net.h"
#include "posix/fs.h"
#include "posix/console.h"
#include "posix/net.h"
#endif


#include "db/sqlite.h"


#endif
