/**
 * @file mega/http.h
 * @brief Generic host HTTP I/O interfaces
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

#ifndef MEGA_HTTP_H
#define MEGA_HTTP_H 1

#include <atomic>
#include "types.h"
#include "waiter.h"
#include "backofftimer.h"
#include "utils.h"

#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#else

#pragma warning(push)
#pragma warning( disable : 4459 )
// um\ws2tcpip.h(738,14): warning C4459: declaration of 'Error' hides global declaration
// winrt\AsyncInfo.h(77,52): message : see declaration of 'Error'
#include <ws2tcpip.h>
#pragma warning(pop)
#endif

#ifdef __FreeBSD__
#include <netinet/in.h>
#endif

namespace mega {

#ifdef _WIN32
    const char* mega_inet_ntop(int af, const void* src, char* dst, int cnt);
#else
    #define mega_inet_ntop inet_ntop
#endif

// SSL public key pinning - active key
#define APISSLMODULUS1 "\xb6\x61\xe7\xcf\x69\x2a\x84\x35\x05\xc3\x14\xbc\x95\xcf\x94\x33\x1c\x82\x67\x3b\x04\x35\x11" \
"\xa0\x8d\xc8\x9d\xbb\x9c\x79\x65\xe7\x10\xd9\x91\x80\xc7\x81\x0c\xf4\x95\xbb\xb3\x26\x9b\x97\xd2" \
"\x14\x0f\x0b\xca\xf0\x5e\x45\x7b\x32\xc6\xa4\x7d\x7a\xfe\x11\xe7\xb2\x5e\x21\x55\x23\x22\x1a\xca" \
"\x1a\xf9\x21\xe1\x4e\xb7\x82\x0d\xeb\x9d\xcb\x4e\x3d\x0b\xe4\xed\x4a\xef\xe4\xab\x0c\xec\x09\x69" \
"\xfe\xae\x43\xec\x19\x04\x3d\x5b\x68\x0f\x67\xe8\x80\xff\x9b\x03\xea\x50\xab\x16\xd7\xe0\x4c\xb4" \
"\x42\xef\x31\xe2\x32\x9f\xe4\xd5\xf4\xd8\xfd\x82\xcc\xc4\x50\xd9\x4d\xb5\xfb\x6d\xa2\xf3\xaf\x37" \
"\x67\x7f\x96\x4c\x54\x3d\x9b\x1c\xbd\x5c\x31\x6d\x10\x43\xd8\x22\x21\x01\x87\x63\x22\x89\x17\xca" \
"\x92\xcb\xcb\xec\xe8\xc7\xff\x58\xe8\x18\xc4\xce\x1b\xe5\x4f\x20\xa8\xcf\xd3\xb9\x9d\x5a\x7a\x69" \
"\xf2\xca\x48\xf8\x87\x95\x3a\x32\x70\xb3\x1a\xf0\xc4\x45\x70\x43\x58\x18\xda\x85\x29\x1d\xaf\x83" \
"\xc2\x35\xa9\xc1\x73\x76\xb4\x47\x22\x2b\x42\x9f\x93\x72\x3f\x9d\x3d\xa1\x47\x3d\xb0\x46\x37\x1b" \
"\xfd\x0e\x28\x68\xa0\xf6\x1d\x62\xb2\xdc\x69\xc7\x9b\x09\x1e\xb5\x47"

// SSL public key pinning - backup key
#define APISSLMODULUS2 "\xaf\xe6\x13\x63\xe6\x24\x7c\x6b\x3c\xfe\x61\x91\x58\x20\xf5\xb9\x91\xdb\x86\x4c\x8e\x0c\x2f" \
"\xdb\x78\x31\xac\xba\x48\x03\xcf\x07\x95\xc6\x09\xda\x5b\xf9\x7b\x60\xa2\x87\xfe\xa9\xa5\xa2\x8a" \
"\x8a\x2c\xb1\x48\xa7\x8e\x66\x24\x0a\xc7\x38\xcf\xba\xdb\x77\x1d\x0b\xe9\xbe\x00\x54\x7f\xe9\x0e" \
"\x56\xbd\xcf\x7c\x10\xf5\xc2\x5f\xc2\x2e\x8f\xbf\x36\xfe\xe0\x5e\x18\xef\xcb\x2f\x88\x95\x4d\xe2" \
"\x72\xed\xfe\x60\x58\x7c\xdf\x75\xb1\x88\x27\xf4\x1c\x9f\xea\x83\x1f\xc6\x34\xa7\x54\x3d\x59\x9d" \
"\x43\xd9\x75\xf4\x17\xcf\x99\x63\x02\xfd\xad\x0f\xc2\x8d\xe7\x0a\xcc\x0c\xda\xac\x99\xc6\xd3\xf5" \
"\xef\xa2\x1f\xd6\xdc\xdb\x98\x63\x2a\xac\x00\x94\x5f\x42\x33\x46\xb6\x10\x86\xcd\x03\x92\xb0\x23" \
"\x2f\x86\x30\x53\xf8\x04\x92\x89\x2e\x0a\x25\x3f\xfa\x4c\x69\xd6\xd7\xaf\x62\xee\xd6\xec\xf8\x96" \
"\xaf\x53\x1a\x13\x33\x38\x7e\xe1\xa9\xe0\x3f\x43\x2f\x17\x05\x90\xe1\x42\xaa\x47\x6d\xef\xdf\x75" \
"\x2e\x3c\xfd\xcf\xbb\x0b\x31\x21\xab\x81\x57\x95\xd3\x04\xf9\x52\x69\x2e\x30\xe5\x45\x2d\x23\x5f" \
"\x6f\x26\x76\x69\x7a\x12\x99\x78\xe0\x08\x87\x33\xd6\x94\xf0\x6c\x6d"

// active and backup keys use the same exponent
#define APISSLEXPONENTSIZE "\x03"
#define APISSLEXPONENT "\x01\x00\x01"

// chatd/presenced SSL public key pinning - chat key (used by MEGAchat)
#define CHATSSLMODULUS "\xbe\x75\xfe\xe1\xff\xac\x69\x2b\xc8\x0c\x12\xe9\x9f\x78\x60\xc2\xa0\xe1\xf1\xf2\xec\x48\xc5" \
"\x8b\xb0\x94\xe9\x68\x02\xdd\xde\xe5\xc3\x15\x53\x55\x44\xc6\x5f\x71\xb3\xe5\x8f\xa3\x8a\x86\x75" \
"\x13\x79\x10\x25\xef\x8c\xc6\x4d\xf0\xbf\x8b\x4a\xfb\x49\x58\xae\xe7\x71\x21\xf4\x29\x58\x28\xb4" \
"\xbf\x41\xec\xa7\x81\xc8\xbe\x64\xd4\xf7\x44\xa2\x0c\x31\x6b\x7c\xfc\x33\x0a\x60\xa8\x36\x5a\xe8" \
"\xfd\xdb\x11\x44\xf8\x69\x12\x4f\x4c\x4a\x48\x2b\x4e\x0a\x44\x1b\xb7\x86\x08\xd9\x5d\x61\x2a\x8b" \
"\x51\x37\x51\x6d\x29\x8c\x4f\xfe\xc2\x84\x2d\x52\x94\xe0\xf4\x60\x5b\xdd\x8d\xda\x67\xe5\xfb\x37" \
"\x77\x51\xc3\x52\xb1\x24\x7f\x46\x3f\x3c\x62\xb5\x1e\xfa\x76\x0f\x39\xaf\x23\xd8\x93\xa9\x4a\x53" \
"\xdf\x38\x59\xde\x70\xbb\x1c\x66\xc8\xbc\xd4\xbc\x1e\xb9\x20\xa6\x62\x9a\x75\xd6\xc9\x94\x46\xcd" \
"\x09\x8f\xa3\x9e\xf9\x1f\xe8\x11\x73\x98\x66\x84\x04\x8f\x7c\xee\xc6\x28\xb3\x21\xa4\x9b\x42\xa3" \
"\xb1\x8f\x0f\xb9\x1a\x4d\xd6\xc0\x26\xa5\x42\x83\x6f\x64\xdf\x8e\x6a\x4e\xf9\x24\x50\x1f\x43\x74" \
"\x42\x43\x0d\x31\x69\xf5\xca\x47\xf8\x82\x8f\xf2\x8b\xc6\xa2\x57\x15"

// chatd/presenced public key pinning - chat backup key (used by MEGAchat)
#define CHATSSLMODULUS2 "\xb4\xf6\x5b\x5e\x17\x79\xd4\x65\xd0\x53\xe7\x2a\x80\x92\x0a\x67\xc3\xb7\xef\xd1\x96\x5c\x3e" \
"\x8f\x7c\xb2\x0f\xe7\xd1\x4a\x11\xb6\xcc\x35\x38\x73\xcd\x29\xf0\xc0\x83\x00\xad\xfb\xd2\x30\xf3" \
"\x5a\xdf\x6f\xd7\xc6\x41\x0e\xd2\xcd\xb4\xad\xfc\x62\x8a\xd2\x8f\x5a\x1d\x05\xb0\x58\x89\x2c\x78" \
"\xdf\xaf\xeb\xdc\xff\x97\x07\x7e\x79\x14\xe3\xea\x05\x2d\x23\x21\x53\xb1\xfd\xb2\xdf\x26\x7d\xa0" \
"\xce\xd7\x7a\x30\x18\x20\x9a\xa7\x13\x74\x13\x40\x3d\x3e\x30\x1c\x34\xf8\x47\xda\x77\xfc\xe2\x68" \
"\x63\x7f\xfa\xb5\x5e\x8c\x6f\x65\x1f\x78\x4e\x9b\x4f\x13\x4f\x35\x5d\x26\x9e\x02\xcd\x9b\x8d\xca" \
"\x56\x6f\x1b\x0a\x73\x2a\x03\x2b\x70\x16\x43\x11\xc3\xfd\xab\xde\xb9\xc5\x80\x4c\x1b\x1b\x94\x25" \
"\x7f\xb5\x0f\x5d\x7e\x89\x01\x73\x77\x93\x9c\x65\x98\xf5\x54\x22\x61\x6b\x9c\x1d\x21\xdc\xe5\x52" \
"\xaa\xcc\xd7\x57\x30\x87\xd4\x45\x33\x3f\xfd\xd9\x0b\xf6\x4e\x15\xe2\x3b\x0a\x0d\x84\xa0\x0a\x5b" \
"\x43\x46\xc1\x3b\x8a\xea\x07\xe9\xc6\xc8\x44\xa3\xa0\x2d\x30\xc7\xaf\xc3\xfb\x76\x28\x59\xad\xf3" \
"\xe4\x7b\x36\x9c\x86\xb9\x32\x5b\x21\x0d\xfc\x47\x01\xee\x4a\xd9\x59"

#define CHATSSLEXPONENTSIZE "\x03"
#define CHATSSLEXPONENT "\x01\x00\x01"


// SFU SSL public key pinning - active key (used by MEGAchat)
#define SFUSSLMODULUS "\xd5\x02\x43\xfa\x00\x9e\xc2\xe4\xbe\x74\xcc\x09\xe7\xa2\xac\x43\xfd\x8a\xa3\x21\xda\x47\x3d" \
"\x27\x0e\x8d\x2d\x0a\xfe\x07\xec\x46\xba\xb5\x07\x47\x54\x45\x05\x28\x46\x27\x43\xf1\x82\x7c\xd9" \
"\x14\x6c\x15\xce\x6e\x23\x46\x60\x4c\x06\x6d\x11\x5e\x86\x05\xd0\x33\x6b\x61\x5d\x6f\xcf\x86\x35" \
"\xbf\x1a\xdd\x85\xf1\xa2\xa3\x19\xe5\xf3\xe8\x24\x8c\x68\x10\x34\x7b\xf0\x52\x21\x56\x8a\x47\x23" \
"\x80\x56\xf2\x6f\xb1\x29\x27\x25\x9e\xe7\x45\x98\x5c\xe2\x31\x2a\x52\x71\x80\xab\xe9\x46\xe7\x71" \
"\x90\x39\x56\x9d\x0f\xf3\x99\x20\x2f\x3d\xac\xd0\xfc\x09\xa2\x69\x1b\xaa\x56\x4c\x4a\xca\xbc\xaf" \
"\x78\xde\xf0\x8e\x5b\x0e\x7b\xd2\xb8\x03\xe0\x1a\x65\xc1\xd8\x4b\x80\x5b\xee\x40\xea\x82\x06\x3b" \
"\xab\xca\x88\xb1\x8e\x57\x6a\xed\x92\x9c\x46\xd9\xbe\xed\xcb\x59\x08\xa1\x7f\x0b\x28\xb3\x61\xa6" \
"\x1d\x20\xe2\x0d\xd8\xcb\xc0\xe7\x94\xae\x8c\xa4\x1f\xab\x0a\x71\xd9\x41\xaa\x9f\x48\x6d\x7b\xd2" \
"\x2f\x5d\x3f\x1d\xd1\x14\x7d\x6c\xb0\xac\xa5\xf5\xba\xb8\xd5\xf2\xd7\x81\x0a\xf5\x4c\x54\x0b\xe9" \
"\x30\x3c\x4c\x77\x41\x30\x9b\xb6\xf0\x3b\xbf\x8c\xcf\xd3\x7f\x3b\xdb"

// SFU SSL public key pinning - backup key (used by MEGAchat)
#define SFUSSLMODULUS2 "\xe2\xc7\x18\x9e\x64\xd2\xe3\x04\x73\xcb\xd8\xa4\xcf\x46\xc2\xa9\x91\x0b\x5f\x83\x5f\x46\x40" \
"\x19\xe3\xd9\xf6\x6f\x28\x88\xa9\x4c\x35\x5e\x83\x20\xb5\x2e\xd3\xb6\x55\x3e\xfc\x7c\x42\x47\x4f" \
"\x20\x6b\x4c\x32\xc9\x25\x44\xf3\x62\x6c\x4d\xdf\x29\xd8\xcc\x99\x90\xfa\xbf\x76\x3b\xf8\x4e\xcb" \
"\x00\x3b\x01\xdd\x4f\x0d\xf6\x4f\xd8\xbd\x2a\x8c\xe0\xf9\x50\x69\x78\xe5\xc1\x4a\x53\x42\xe9\x67" \
"\xe6\xab\x16\xd7\x27\x4b\x95\x25\xec\xd0\x34\xcb\x52\x36\xa3\x74\xbb\xef\xbd\x9a\x95\x61\x27\x57" \
"\x66\xe5\xd0\x4e\x2a\x7a\x50\x68\x0b\x7e\x2a\x09\xee\xeb\x7f\xb3\x35\x75\x21\x36\x37\x2f\x36\xb4" \
"\x71\x11\x0f\x56\x57\xef\xb5\xeb\xb4\x65\xf2\x30\x2f\x33\x0b\x13\x9b\x79\x77\xb2\x69\x5b\x34\x9b" \
"\x59\x87\x14\xea\x92\xc8\x43\x99\x93\x5e\x3d\x6f\x8b\xba\x5f\xda\xd8\x39\xf0\x66\xba\x48\x29\xa2" \
"\x1e\xf4\x4e\xcb\xd6\x65\x6a\x34\x9c\xfa\x73\x64\x99\x43\xc9\x46\x73\x4c\x62\x5b\x78\x50\xbd\x41" \
"\xb1\xab\x0d\x62\xbf\x85\x70\x61\x09\x29\xf9\x67\x95\x13\xb9\xdc\xc3\x37\xde\xf0\x5f\x5e\x60\x17" \
"\x25\x30\x66\x28\x36\x60\x1e\xc0\x0f\x2d\x36\xd8\x6e\x90\xe2\xa9\xa1"

// active and backup keys use the same exponent
#define SFUSSLEXPONENTSIZE "\x03"
#define SFUSSLEXPONENT "\x01\x00\x01"

// SFU-stats SSL public key pinning - active key
#define SFUSTATSSSLMODULUS "\xaf\x59\x51\xf0\x25\x45\x96\x7f\x49\x1e\x39\xdd\xc6\xd5\xeb\x0e\xc7\x8f\xa5\x38\x33\xf3\x54" \
"\x2e\x64\xf2\x6a\x67\xba\x11\xd7\xef\x64\x76\x4e\x7b\x5c\x97\xcb\x88\xf3\x40\x64\xb2\x37\x2e\xbe" \
"\x63\x98\x9c\xc0\x6d\xf8\x69\xfd\xb8\x63\xb1\x5d\x34\xcd\xf8\x1d\xf9\xf1\xa4\x56\x62\xfd\x20\x0d" \
"\x04\xbf\x30\xac\x71\x90\x89\x59\x4d\x51\x9f\x93\xae\xcd\xf4\x50\xd1\xfd\x69\x3f\xd7\xb7\x00\x98" \
"\x59\x98\x0a\xbe\xbc\x78\x6d\xee\x14\x32\x46\x6f\x58\x6f\xe4\x57\xe5\xf5\xe6\x2b\xb6\x50\xaf\x90" \
"\x19\x04\x29\x97\xc6\xba\x4c\x33\x87\x29\x23\xcc\xa2\xa5\x34\x01\x4f\xe7\xba\xbf\x81\x94\x7d\x39" \
"\xe0\x67\xb7\xbe\x6e\x10\x4e\x91\x64\x7b\x8a\x20\x10\xb9\x07\x77\x0b\xe5\xfb\x0d\x49\x51\xbb\x36" \
"\xed\x65\x06\x36\xe3\x64\xf3\x5f\x5f\x59\x0b\x4f\x49\x83\xc7\xf8\xe1\x6c\x79\x25\x91\xa0\xbc\x00" \
"\xda\xe1\x95\xed\x4c\xb0\xc5\x29\xba\xb4\xe0\xef\x6a\xb7\x2c\xeb\xa4\xbf\x2b\xac\xe3\x52\xe0\xd5" \
"\x81\xde\x4c\xba\x79\x9f\x45\x3b\x07\x3f\x55\xd2\xa1\xf3\x94\xaa\x9a\x5a\x5b\xb9\x17\x64\x2e\xbf" \
"\x2a\xb2\x3d\x4c\xa2\x95\x13\x9a\x57\xfd\xae\x69\x44\x77\x64\x12\x3d"

// SFU-stats SSL public key pinning - backup key
#define SFUSTATSSSLMODULUS2 "\x9f\x3a\xa7\x48\x3b\x71\xbf\x20\xc5\x32\x79\x46\xb1\xa3\x01\xb8\xd8\x07\x27\x0e\x6f\xe5\x2c" \
"\xb1\x0d\xd2\x3f\x6f\x92\x99\xb3\x7c\xb9\x4d\xf5\x7e\xbc\x21\x4b\x87\xbe\x93\x7d\xb9\xb2\x55\x5d" \
"\xd0\x9e\x1c\xd8\x19\x74\x68\x05\x90\x15\x93\x2b\x3d\x06\x0d\xeb\x5d\x52\xa7\xf9\x03\x33\x1f\x84" \
"\x52\x71\xe0\x05\x4d\x97\x36\x79\x9d\x29\x79\xb2\x79\x10\x64\x67\xb0\xdf\xa1\xda\x9e\x31\x92\x80" \
"\xaf\x36\x7d\x06\xae\x28\xac\xc9\x33\x9d\x1e\x82\xf2\xbe\x08\x7a\xa0\x35\x74\xd6\xb3\x94\xe3\x34" \
"\x0f\xc2\x69\x5a\xf3\xea\xee\x72\x78\xba\x46\xe2\x45\xde\x9a\x52\x9b\x8b\x54\xce\x71\xd8\x5b\x5b" \
"\x96\xbe\xce\xae\x0e\x58\x21\x1d\xa8\x01\x76\x87\xa0\x9e\x46\x61\xbe\x3d\xc6\xcc\xc3\x3d\x76\xf8" \
"\x61\xaa\xaf\x68\x8e\xf7\x50\xf4\x6e\xca\x1d\x4f\xf1\xc3\xbf\xb0\x3f\x50\x8b\x2d\x22\xbf\x95\x0a" \
"\x39\x8f\xd6\x9d\x3d\x42\xbe\x39\x65\xf2\xd9\xf4\x8c\xb5\x7c\x28\x0a\xf3\xe4\x88\xbb\x43\x21\x97" \
"\xfe\xbd\x27\x40\xea\xba\x08\xa6\x83\x60\x50\x1b\x06\xe1\x82\xb2\x4f\xc2\xee\xf5\x9e\xab\x43\xc7" \
"\xc7\x3b\xf6\xc6\xd3\xcc\xff\x9e\xd9\xa3\x3a\x7b\x18\x00\xd3\xca\xfd"

// active and backup keys use the same exponent
#define SFUSTATSSSLEXPONENTSIZE "\x03"
#define SFUSTATSSSLEXPONENT "\x01\x00\x01"


#define DNS_SERVERS "2001:4860:4860::8888,8.8.8.8," \
                    "2001:4860:4860::8844,8.8.4.4," \
                    "2606:4700:4700::1111,1.1.1.1," \
                    "2606:4700:4700::1001,1.0.0.1," \
                    "2620:fe::fe,9.9.9.9"

class MEGA_API SpeedController
{
public:
    // Size of the circular buffer used to calculate the circular mean speed (in seconds).
    static const dstime SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS = 5;
    // Constant representing the number of deciseconds in one second.
    static constexpr dstime DS_PER_SECOND = 10;

    SpeedController();

    // Calculates and updates the circular mean speed and the total mean speed.
    // Returns the circular mean speed (same value as getMeanSpeed()).
    m_off_t calculateSpeed(m_off_t numBytes);
    // Retrieves the circular mean speed calculated over the time window defined by the circular buffer (SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS).
    m_off_t getCircularMeanSpeed() const;
    // Retrieves the total mean speed calculated over the entire data transfer duration.
    m_off_t getMeanSpeed() const;

    // Initialize the speed values. If called again, it increments the initial time by the time elapsed since the last update.
    void requestStarted();
    // Calculates the speed (calls calculateSpeed) using "newPos" as the delta value, and returns the delta (newPos - mRequestPos).
    m_off_t requestProgressed(m_off_t newPos);
    // Get the last request speed.
    m_off_t lastRequestMeanSpeed() const;
    // Time elapsed since the request started in deciseconds.
    dstime requestElapsedDs() const;

private:
    // Values for the circular mean speed
    std::array<m_off_t, SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS> mCircularBuf{}; // Circular buffer of bytes received/transmitted per second.
    size_t mCircularCurrentIndex{}; // Index of the current entry in the circular buffer.
    dstime mCircularCurrentTime{}; // Current time for circular buffer updates.
    m_off_t mCircularCurrentSum{}; // Sum of bytes in the circular buffer.

    // Values for the total mean speed
    m_off_t mTotalSumBytes{}; // Total sum of bytes received/transmitted.
    dstime mInitialTime{}; // Initial time when the speed controller was started.
    m_off_t mMeanSpeed{}; // Total mean speed (retrieved with getMeanSpeed()).

    // Values for single requests
    m_off_t mRequestPos{}; // Position of the single request.
    dstime mRequestStart{}; // Start time of the single request.
    dstime mLastRequestUpdate{}; // Last time the single request was updated.

    // Calculate the total mean speed by aggregating progress (from deciseconds to seconds) over the total time period.
    // Helper method to be called within calculateSpeed(), so the value is assigned to mMeanSpeed, which can be retrieved with getMeanSpeed().
    m_off_t calculateMeanSpeed();
    // Helper method to update the circular buffer when the delta time from the previous call is within the circular buffer size (SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS)
    void updateCircularBufferWithinLimit(m_off_t delta, dstime deltaTimeFromPreviousCall);
    // Helper method to update the circular buffer when the delta time from the previous call exceeds the circular buffer size (SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS)
    void updateCircularBufferWithWeightedAverageForDeltaExceedingLimit(m_off_t& delta, dstime deltaTimeFromPreviousCall);
    // Calculate offset in deciseconds for the current second starting from the initial time.
    dstime calculateCurrentSecondOffsetInDs() const;
    // Calculate next buffer index.
    void nextIndex(size_t &currentCircularBufIndex, size_t positionsToAdvance = 1) const;
    // Aggregate instantaneous delta values over a specified time subperiod to calculate a weighted average.
    m_off_t aggregateProgressForTimePeriod(dstime timePeriodToAggregate, dstime totalTime, m_off_t bytesToAggregate) const;
};

extern std::mutex g_APIURL_default_mutex;
extern string g_APIURL_default;
extern bool g_disablepkp_default;

// generic host HTTP I/O interface
struct MEGA_API HttpIO : public EventTrigger
{
    // set whenever a network request completes successfully
    bool success;

    // post request to target URL
    virtual void post(struct HttpReq*, const char* = NULL, unsigned = 0) = 0;

    // cancel request
    virtual void cancel(HttpReq*) = 0;

    // real-time POST progress information
    virtual m_off_t postpos(void*) = 0;

    // execute I/O operations
    virtual bool doio(void) = 0;

    // lock/unlock all in-flight HttpReqs
    virtual void lock() { }
    virtual void unlock() { }

    virtual void disconnect() { }

    // track Internet connectivity issues
    dstime noinetds;
    bool inetback;
    void inetstatus(bool);
    bool inetisback();

    // timestamp of last data received (across all connections)
    dstime lastdata;

    // download speed
    SpeedController downloadSpeedController;
    m_off_t downloadSpeed;
    void updatedownloadspeed(m_off_t size = 0);

    // upload speed
    SpeedController uploadSpeedController;
    m_off_t uploadSpeed;
    void updateuploadspeed(m_off_t size = 0);

    // data receive timeout (ds)
    static const int NETWORKTIMEOUT;

    // request timeout (ds)
    static const int REQUESTTIMEOUT;

    // sc request timeout (ds)
    static const int SCREQUESTTIMEOUT;

    // connection timeout (ds)
    static const int CONNECTTIMEOUT;

    // root URL for API requests
    string APIURL;

    // disable public key pinning (for testing purposes) (determines if we check the public key from APIURL)
    bool disablepkp = false;

    // set useragent (must be called exactly once)
    virtual void setuseragent(string*) = 0;

    // get proxy settings from the system
    virtual Proxy *getautoproxy();

    // get DNS servers as configured in the system
    void getDNSserversFromIos(string &dnsServers);
    
    // get alternative DNS servers
    void getMEGADNSservers(string* dnsservers, bool getfromnetwork);

    // set max download speed
    virtual bool setmaxdownloadspeed(m_off_t bpslimit);

    // set max upload speed
    virtual bool setmaxuploadspeed(m_off_t bpslimit);

    // get max download speed
    virtual m_off_t getmaxdownloadspeed();

    // get max upload speed
    virtual m_off_t getmaxuploadspeed();

    virtual bool cacheresolvedurls(const std::vector<string>&, std::vector<string>&&) { return false; }

    HttpIO();
    virtual ~HttpIO() { }
};

// outgoing HTTP request
struct MEGA_API HttpReq
{
    std::atomic<reqstatus_t> status;
    m_off_t pos;

    int httpstatus;

    httpmethod_t method;
    contenttype_t type;
    int timeoutms;

    string posturl;

    bool protect; // check pinned public key
    bool minspeed;
    bool mExpectRedirect = false;
    bool mChunked = false;

    bool sslcheckfailed;
    string sslfakeissuer;
    string mRedirectURL;

    string* out;
    string in;
    size_t inpurge;
    size_t outpos;

    string outbuf;

    // if the out payload includes a fetch nodes command
    bool includesFetchingNodes = false;

    byte* buf;
    m_off_t buflen, bufpos, notifiedbufpos;

    // When did a post() start
    std::chrono::steady_clock::time_point postStartTime;

    // we assume that API responses are smaller than 4 GB
    m_off_t contentlength;

    // time left related to a bandwidth overquota
    m_time_t timeleft;

    // Content-Type of the response
    string contenttype;

    // Hashcash of a response
    string hashcash;
    uint8_t hashcashEasyness{};

    // HttpIO implementation-specific identifier for this connection
    void* httpiohandle;

    // while this request is in flight, points to the application's HttpIO
    // object - NULL otherwise
    HttpIO* httpio;

    // identify different channels from different MegaClients etc in the log
    string logname;

    // set url and content type for subsequent requests
    void setreq(const char*, contenttype_t);

    // send POST request to the network
    void post(MegaClient*, const char* = NULL, unsigned = 0);

    // send GET request to the network
    void get(MegaClient*);

    // send a DNS request
    void dns(MegaClient*);

    // store chunk of incoming data with optional purging
    void put(void*, unsigned, bool = false);

    // start and size of unpurged data block - must be called with !buf and httpio locked
    char* data();
    size_t size();

    // a buffer that the HttpReq filled in.   This struct owns the buffer (so HttpReq no longer has it).
    struct http_buf_t
    {
        byte* datastart() const;
        size_t datalen() const;

        size_t start;
        size_t end;

        http_buf_t(byte* b, size_t s, size_t e);  // takes ownership of the byte*, which must have been allocated with new[]
        ~http_buf_t();
        void swap(http_buf_t& other);
        bool isNull() const;

    private:
        byte* buf;
    };

    // give up ownership of the buffer for client to use.  The caller is the new owner of the http_buf_t, and the HttpReq no longer has the buffer or any info about it.
    http_buf_t* release_buf();

    // set amount of purgeable data at 0
    void purge(size_t);

    // set response content length
    void setcontentlength(m_off_t);

    // reserve space for incoming data
    byte* reserveput(unsigned* len);

    // disconnect open HTTP connection
    void disconnect();

    // progress information
    virtual m_off_t transferred(MegaClient*);

    // timestamp of last data sent or received
    dstime lastdata;

    // prevent raw data from being dumped in debug mode
    bool binary;

    HttpReq(bool = false);
    virtual ~HttpReq();
    void init();

    // get HTTP method as a static string
    const char* getMethodString();

    // true if HTTP response status code is 3xx redirection
    bool isRedirection() const { return (httpstatus / 100) == 3; }
};

struct MEGA_API GenericHttpReq : public HttpReq
{
    GenericHttpReq(PrnGen &rng, bool = false);

    // tag related to the request
    int tag;

    // max number of retries, including the first attempt
    // 0 = infinite retries, 1 = no retries
    int maxretries;

    // current retry number
    int numretry;

    // backoff between retries
    BackoffTimer bt;

    // true when the backoff between retries is active
    bool isbtactive;

    // backoff to control the maximum allowed time for the request
    BackoffTimer maxbt;
};

class MEGA_API EncryptByChunks
{
    // this class allows encrypting a large buffer chunk by chunk,
    // or alternatively encrypting consecutive data by feeding it a piece at a time,
    // from separate buffers (the algorithm chooses the size though)

public:
    // size (in bytes) of the CRC of uploaded chunks
    enum { CRCSIZE = 12 };

    EncryptByChunks(SymmCipher* k, chunkmac_map* m, uint64_t iv);

    // encryption: data must be NULL-padded to SymmCipher::BLOCKSIZE
    // (so buffer allocation size must be rounded up too)
    // len must be < 2^31
    virtual byte* nextbuffer(unsigned datasize) = 0;

    bool encrypt(m_off_t pos, m_off_t npos, string& urlSuffix);

private:
    SymmCipher* key;
    chunkmac_map* macs;
    uint64_t ctriv;     // initialization vector for CTR mode
    byte crc[CRCSIZE];
    void updateCRC(byte* data, unsigned size, unsigned offset);
};

class MEGA_API EncryptBufferByChunks : public EncryptByChunks
{
    // specialisation for encrypting a whole contiguous buffer by chunks
    byte *chunkstart;

    byte* nextbuffer(unsigned bufsize) override;

public:
    EncryptBufferByChunks(byte* b, SymmCipher* k, chunkmac_map* m, uint64_t iv);
};

// file chunk I/O
struct MEGA_API HttpReqXfer : public HttpReq
{
    unsigned size;
    double mStartTransferTime{-1};
    double mConnectTime{-1};
    bool isLatencyProcessed{};

    virtual void prepare(const char*, SymmCipher*, uint64_t, m_off_t, m_off_t) = 0;

    HttpReqXfer() : HttpReq(true), size(0) { }
};

// file chunk upload
struct MEGA_API HttpReqUL : public HttpReqXfer
{
    chunkmac_map mChunkmacs;

    void prepare(const char*, SymmCipher*, uint64_t, m_off_t, m_off_t);

    m_off_t transferred(MegaClient*);

    ~HttpReqUL() { }
};

// file chunk download
struct MEGA_API HttpReqDL : public HttpReqXfer
{
    m_off_t dlpos;
    bool buffer_released;

    void prepare(const char*, SymmCipher*, uint64_t, m_off_t, m_off_t);

    HttpReqDL();
    ~HttpReqDL() { }
};

// file attribute get
struct MEGA_API HttpReqGetFA : public HttpReq
{
    ~HttpReqGetFA() { }
};
} // namespace

#endif
