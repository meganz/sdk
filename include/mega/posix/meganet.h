/**
 * @file mega/posix/meganet.h
 * @brief POSIX network access layer (using cURL + c-ares)
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
#define HTTPIO_CLASS CurlHttpIO

#include "mega/http.h"

#if defined(_WIN32) && !defined(WINDOWS_PHONE)
#include "mega/win32/megawaiter.h"
#endif

#if !defined(USE_CURL_PUBLIC_KEY_PINNING) || defined(WINDOWS_PHONE)
#include <openssl/ssl.h>
#endif

#include <curl/curl.h>
#include <ares.h>

namespace mega {

#if defined(_WIN32) && !defined(WINDOWS_PHONE)
struct MEGA_API SockInfo
{
    enum
    {
        NONE = 0,
        READ = 1,
        WRITE = 2
    };

    SockInfo();
    int fd;
    int mode;
    HANDLE handle;
};
#endif

struct MEGA_API CurlDNSEntry;
struct MEGA_API CurlHttpContext;
class CurlHttpIO: public HttpIO
{
protected:
    string useragent;
    CURLM* curlm;
    CURLSH* curlsh;
    ares_channel ares;
    string proxyurl;
    string proxyscheme;
    string proxyhost;
    int proxyport;
    string proxyip;
    string proxyusername;
    string proxypassword;
    int proxyinflight;
    dstime ipv6deactivationtime;
    dstime lastdnspurge;
    bool ipv6proxyenabled;
    bool ipv6requestsenabled;
    std::queue<CurlHttpContext *> pendingrequests;
    std::map<string, CurlDNSEntry> dnscache;

    void send_pending_requests();
    void drop_pending_requests();

    static size_t read_data(void*, size_t, size_t, void*);
    static size_t write_data(void*, size_t, size_t, void*);
    static size_t check_header(void*, size_t, size_t, void*);

#if defined(_WIN32) && !defined(WINDOWS_PHONE)
    static int socket_callback(CURL *e, curl_socket_t s, int what, void *userp, void *socketp);
    static int timer_callback(CURLM *multi, long timeout_ms, void *userp);
#endif

#if !defined(USE_CURL_PUBLIC_KEY_PINNING) || defined(WINDOWS_PHONE)
    static CURLcode ssl_ctx_function(CURL*, void*, void*);
    static int cert_verify_callback(X509_STORE_CTX*, void*);
#endif

    static void proxy_ready_callback(void*, int, int, struct hostent*);
    static void ares_completed_callback(void*, int, int, struct hostent*);
    static void send_request(CurlHttpContext*);
    void request_proxy_ip();
    static struct curl_slist* clone_curl_slist(struct curl_slist*);
    static bool crackurl(string*, string*, string*, int*);
    static int debug_callback(CURL*, curl_infotype, char*, size_t, void*);
    bool ipv6available();
    void filterDNSservers();

    bool curlipv6;
    bool reset;
    bool statechange;
    string dnsservers;
    curl_slist* contenttypejson;
    curl_slist* contenttypebinary;
    WAIT_CLASS* waiter;

#if defined(_WIN32) && !defined(WINDOWS_PHONE)
    void addaresevents(WinWaiter *waiter);
    void addcurlevents(WinWaiter *waiter);
    std::vector<SockInfo> aressockets;
    std::map<int, SockInfo> curlsockets;
    m_time_t curltimeoutms;
    m_time_t arestimeoutds;
#endif

public:
    void post(HttpReq*, const char* = 0, unsigned = 0);
    void cancel(HttpReq*);
    void sendchunked(HttpReq*);

    m_off_t postpos(void*);

    bool doio(void);

    void addevents(Waiter*, int);

    void setuseragent(string*);
    void setproxy(Proxy*);
    Proxy* getautoproxy();
    void setdnsservers(const char*);
    void disconnect();

    CurlHttpIO();
    ~CurlHttpIO();
};

struct MEGA_API CurlHttpContext
{
    CURL* curl;

    HttpReq* req;
    CurlHttpIO* httpio;

    struct curl_slist *headers;
    bool isIPv6;
    string hostname;
    string scheme;
    int port;
    string hostheader;
    string hostip;
    string posturl;
    unsigned len;
    const char* data;
    int ares_pending;
};

struct MEGA_API CurlDNSEntry
{
    CurlDNSEntry();

    string ipv4;
    dstime ipv4timestamp;
    string ipv6;
    dstime ipv6timestamp;
};

} // namespace

#endif
