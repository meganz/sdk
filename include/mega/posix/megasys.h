/**
 * @file mega/posix/megasys.h
 * @brief Mega SDK platform-specific includes (Posix)
 *
 * (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
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

#ifndef MEGA_POSIX_OS_H
#define MEGA_POSIX_OS_H 1

// platform dependent constants
#if defined(__ANDROID__) && !defined(HAVE_SDK_CONFIG_H)
#include "mega/config-android.h"
#else
#ifndef MEGA_GENERATED_CONFIG_H
#include "mega/config.h"
#define MEGA_GENERATED_CONFIG_H
#endif
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include <inttypes.h>

#include <iostream>
#include <algorithm>
#include <array>
#include <string>   // the MEGA SDK assumes writable, contiguous string::data()
#include <sstream>
#include <fstream>
#include <map>
#include <deque>
#include <set>
#include <iterator>
#include <queue>
#include <list>
#include <functional>

#ifdef HAVE_STDDEF_H
    #include <stddef.h>
#endif

#include <stdio.h>

#ifdef HAVE_STDLIB_H
    #include <stdlib.h>
#endif

#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <memory.h>
#include <time.h>
#include <math.h>

#ifdef HAVE_STDBOOL_H
    #include <stdbool.h>
#endif

#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>

#ifdef HAVE_GLOB_H
    #include <glob.h>
#else
    #include "mega/mega_glob.h"
#endif

#ifdef HAVE_DIRENT_H
    #include <dirent.h>
#endif

#include <sys/un.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <termios.h>

#if defined(__linux__) || defined(__OpenBSD__)
#include <endian.h>
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__minix) || \
    defined(__OpenBSD__)
#include <sys/endian.h>
#endif

#ifdef HAVE_SENDFILE
    #include <sys/sendfile.h>
#endif

#ifdef USE_INOTIFY
    #include <sys/inotify.h>
#endif

#include <sys/select.h>

#include <curl/curl.h>
#include <stdexcept>


#ifndef USE_POLL
#ifndef FD_COPY
#define FD_COPY(s, d) ( memcpy(( d ), ( s ), sizeof( fd_set )))
#endif
#endif

#endif // MEGA_POSIX_OS_H
