#ifndef MEGA_SCCR_SYSTEM_H
#define MEGA_SCCR_SYSTEM_H 1


//#define _POSIX_SOURCE
#define _LARGE_FILES
//#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <syslog.h>
#include <sys/sendfile.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <memory.h>
#include <mcheck.h>
#include <mqueue.h>
#include <endian.h>
#include <dirent.h>

#ifndef htobe64
#define htobe64(x) (((uint64_t) htonl((uint32_t) ((x) >> 32))) | (((uint64_t) htonl((uint32_t) x)) << 32))
#endif

#include <thread>

#include <iostream>
#include <algorithm>
#include <string>
#include <map>
#include <set>
#include <list>
#include <iterator>
#include <queue>
#include <tr1/unordered_set>
#include <climits>

#include <openssl/ssl.h>

//using namespace std;


// basic types
typedef uint64_t token_t;
typedef uint64_t userid_t;
typedef uint64_t sn_t;

typedef uint64_t mtime_t;
typedef uint8_t opcode_t;
typedef uint32_t connid_t;
typedef uint32_t count_t;
typedef uint8_t version_t;

typedef unsigned int uint128_t __attribute__((mode(TI)));

#include "mega/types.h"

namespace mega::SCCR {
    using raidTime = mega::dstime;
}

#endif