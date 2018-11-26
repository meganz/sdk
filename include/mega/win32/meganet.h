/**
 * @file mega/win32/meganet.h
 * @brief Win32 network access layer (using WinHTTP)
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

#ifndef HTTPIO_CLASS
#define HTTPIO_CLASS WinHttpIO
#define DONT_RELEASE_HTTPIO

#include "zlib.h"
#include "mega.h"

typedef LPVOID HINTERNET;   // from <winhttp.h>

// MinGW shipped winhttp.h does not have these two flags
#ifdef __MINGW32__
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 0x00000200
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif
#endif

namespace mega {
extern bool debug;

class MEGA_API WinHttpIO: public HttpIO
{
    CRITICAL_SECTION csHTTP;
    HANDLE hWakeupEvent;

protected:
    WinWaiter* waiter;
    HINTERNET hSession;
    string proxyUsername;
    string proxyPassword;

public:
    static const unsigned HTTP_POST_CHUNK_SIZE = 16384;

    static VOID CALLBACK asynccallback(HINTERNET, DWORD_PTR, DWORD,
                                       LPVOID lpvStatusInformation,
                                       DWORD dwStatusInformationLength);

    void updatedstime();

    void post(HttpReq*, const char* = 0, unsigned = 0);
    void cancel(HttpReq*);

    m_off_t postpos(void*);

    bool doio(void);

    void addevents(Waiter*, int);

    void lock();
    void unlock();

    void httpevent();

    void setuseragent(string*);
    void setproxy(Proxy *);

    WinHttpIO();
    ~WinHttpIO();
};

struct MEGA_API WinHttpContext
{
    HINTERNET hRequest;
    HINTERNET hConnect;

    HttpReq* req;                   // backlink to underlying HttpReq
    WinHttpIO* httpio;              // backlink to application-wide WinHttpIO object

    unsigned postpos;
    unsigned postlen;
    const char* postdata;
    
    bool gzip;
    z_stream z;
    string zin;
};
} // namespace

#endif
