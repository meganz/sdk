/*

MEGA SDK - Client Access Engine Core Logic

(c) 2013 by Mega Limited, Wellsford, New Zealand

Applications using the MEGA API must present a valid application key
and comply with the the rules set forth in the Terms of Service.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#ifndef MEGA_H
#define MEGA_H 1

#ifndef MEGA_SDK
#define MEGA_SDK
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <memory.h>
#include <time.h>

#ifdef _WIN32

#include <windows.h>
#define atoll _atoi64
#define snprintf _snprintf
#define _CRT_SECURE_NO_WARNINGS

#else

#include <unistd.h>
#include <arpa/inet.h>

#ifndef __MACH__
#include <endian.h>
#endif

#endif

// FIXME: #define PRI*64 if missing
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

typedef int64_t m_off_t;

#include <iostream>
#include <algorithm>
#include <string>
#include <sstream>
#include <map>
#include <set>
#include <iterator>
#include <queue>
#include <list>

#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>


// XXX: posix
//#define _POSIX_SOURCE
#define _LARGE_FILES
//#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64

#include <sys/sendfile.h>
#include <sys/un.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>

#include <curl/curl.h>
#include <sys/select.h>

#include <sys/inotify.h>
#include <glob.h>
#include <dirent.h>

#include <sys/select.h>
#include <sys/inotify.h>


#define __DARWIN_C_LEVEL 199506L

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



// monotonously increasing time in deciseconds
typedef uint32_t dstime;

using namespace std;

#include "crypto/cryptopp.h"
#include "megaclient.h"
#include "posix/net.h"
#include "db/sqlite.h"
#include "posix/fs.h"
#include "posix/console.h"
#include "posix/net.h"


#endif
