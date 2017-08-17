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

#include "mega.h"

#ifdef USE_OPENSSL
#include <openssl/ssl.h>
#endif

#include <curl/curl.h>
#include <ares.h>

namespace mega {

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
#if defined(_WIN32)
    HANDLE handle;
#endif
};

struct MEGA_API CurlDNSEntry;
struct MEGA_API CurlHttpContext;
class CurlHttpIO: public HttpIO
{
protected:
    static MUTEX_CLASS curlMutex;

    string useragent;
    CURLM* curlm[3];

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
    int pkpErrors;

    void send_pending_requests();
    void drop_pending_requests();

    static size_t read_data(void*, size_t, size_t, void*);
    static size_t write_data(void*, size_t, size_t, void*);
    static size_t check_header(void*, size_t, size_t, void*);
    static int seek_data(void*, curl_off_t, int);

    static int socket_callback(CURL *e, curl_socket_t s, int what, void *userp, void *socketp, direction_t d);
    static int api_socket_callback(CURL *e, curl_socket_t s, int what, void *userp, void *socketp);
    static int download_socket_callback(CURL *e, curl_socket_t s, int what, void *userp, void *socketp);
    static int upload_socket_callback(CURL *e, curl_socket_t s, int what, void *userp, void *socketp);
    static int timer_callback(CURLM *multi, long timeout_ms, void *userp, direction_t d);
    static int api_timer_callback(CURLM *multi, long timeout_ms, void *userp);
    static int download_timer_callback(CURLM *multi, long timeout_ms, void *userp);
    static int upload_timer_callback(CURLM *multi, long timeout_ms, void *userp);

#if defined(USE_OPENSSL) && !defined(OPENSSL_IS_BORINGSSL)
    static MUTEX_CLASS **sslMutexes;
    static void locking_function(int mode, int lockNumber, const char *, int);

#if OPENSSL_VERSION_NUMBER >= 0x10000000
    static void id_function(CRYPTO_THREADID* id);
#else
    static unsigned long id_function();
#endif
#endif

#ifdef USE_OPENSSL
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
    bool dnsok;
    string dnsservers;
    curl_slist* contenttypejson;
    curl_slist* contenttypebinary;
    WAIT_CLASS* waiter;

    void addaresevents(Waiter *waiter);
    void addcurlevents(Waiter *waiter, direction_t d);
    void closearesevents();
    void closecurlevents(direction_t d);
    void processaresevents();
    void processcurlevents(direction_t d);
    std::vector<SockInfo> aressockets;
    std::map<int, SockInfo> curlsockets[3];
    m_time_t curltimeoutreset[3];
    bool arerequestspaused[3];
    int numconnections[3];
    set<CURL *>pausedrequests[3];
    m_off_t partialdata[2];
    m_off_t maxspeed[2];
    bool curlsocketsprocessed;
    m_time_t arestimeout;

public:
    void post(HttpReq*, const char* = 0, unsigned = 0);
    void cancel(HttpReq*);

    m_off_t postpos(void*);

    bool doio(void);
    bool multidoio(CURLM *curlmhandle);

    void addevents(Waiter*, int);

    void setuseragent(string*);
    void setproxy(Proxy*);
    void setdnsservers(const char*);
    void disconnect();

    // set max download speed
    virtual bool setmaxdownloadspeed(m_off_t bpslimit);

    // set max upload speed
    virtual bool setmaxuploadspeed(m_off_t bpslimit);

    // get max download speed
    virtual m_off_t getmaxdownloadspeed();

    // get max upload speed
    virtual m_off_t getmaxuploadspeed();

    CurlHttpIO();
    ~CurlHttpIO();
};

struct MEGA_API CurlHttpContext
{
    CURL* curl;
    direction_t d;

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
