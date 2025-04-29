/**
 * @file mega/posix/meganet.h
 * @brief POSIX network access layer (using cURL)
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

#pragma once

#include "mega.h"

#ifdef USE_OPENSSL
#include <openssl/ssl.h>
#endif

#include <curl/curl.h>

namespace mega {

extern std::atomic<bool> g_netLoggingOn;

struct MEGA_API SockInfo
{
    enum
    {
        NONE = 0,
        READ = 1,
        WRITE = 2
    };

#ifdef WIN32
    SockInfo(HANDLE& sharedEvent) : mSharedEvent(sharedEvent) { }
#else
    SockInfo() = default;
#endif
    curl_socket_t fd = curl_socket_t(-1);
    int mode = NONE;

#if defined(_WIN32)
    SockInfo(const SockInfo&) = delete;
    void operator=(const SockInfo&) = delete;
    SockInfo(SockInfo&& o);
    ~SockInfo();

    // create the event and call WSAEventSelect, if it hasn't been done yet.
    bool createAssociateEvent();

    // see if there is any work to be done on this socket (to be called after waiting, and a network event was triggered)
    bool checkEvent(bool& read, bool& write, bool logErr = true);

    // manually close the event (used when we know the socket is no longer active)
    void closeEvent(bool adjustSocket = true);

    // get the event handle, for waiting on
    HANDLE sharedEventHandle();

    // Flag for dealing with windows write event signalling, where we only get signalled if the socket goes from unwriteable to writeable (but not if we wrote to it and didn't get it to the unwriteable state)
    bool signalledWrite = false;

private:
    HANDLE& mSharedEvent;
    int associatedHandleEvents = 0;
#endif
};

struct MEGA_API CurlDNSEntry;
struct MEGA_API CurlHttpContext;
class CurlHttpIO: public HttpIO
{
protected:
    static std::mutex curlMutex;

    string useragent;
    CURLM* curlm[3];

    CURLSH* curlsh;
    string proxyurl;
    string proxyscheme;
    string proxyhost;
    int proxyport;
    int proxytype;
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
    static size_t check_header(const char*, size_t, size_t, void*);
    static int seek_data(void*, curl_off_t, int);

    static int socket_callback(CURL *e, curl_socket_t s, int what, void *userp, void *socketp, direction_t d);
    static int sockopt_callback(void *clientp, curl_socket_t curlfd, curlsocktype purpose);
    static int api_socket_callback(CURL *e, curl_socket_t s, int what, void *userp, void *socketp);
    static int download_socket_callback(CURL *e, curl_socket_t s, int what, void *userp, void *socketp);
    static int upload_socket_callback(CURL *e, curl_socket_t s, int what, void *userp, void *socketp);
    static int timer_callback(CURLM *multi, long timeout_ms, void *userp, direction_t d);
    static int api_timer_callback(CURLM *multi, long timeout_ms, void *userp);
    static int download_timer_callback(CURLM *multi, long timeout_ms, void *userp);
    static int upload_timer_callback(CURLM *multi, long timeout_ms, void *userp);

#if defined(USE_OPENSSL) && !defined(OPENSSL_IS_BORINGSSL)
public: // so we can delete it on program end
    static std::recursive_mutex **sslMutexes;
protected:
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

    static void send_request(CurlHttpContext*);
    void request_proxy_ip();
    static struct curl_slist* clone_curl_slist(struct curl_slist*);
    static bool crackurl(const string*, string*, string*, int*);
    static int debug_callback(CURL*, curl_infotype, char*, size_t, void*);
    bool ipv6available();
    bool curlipv6;
    bool reset;
    bool statechange;
    bool dnsok;
    string dnsservers;
    curl_slist* contenttypejson;
    curl_slist* contenttypebinary;
    WAIT_CLASS* waiter;
    bool disconnecting;

    typedef std::map<curl_socket_t, SockInfo> SockInfoMap;

    void addcurlevents(Waiter* eventWaiter, direction_t d);
    int checkevents(Waiter*) override;
    void closecurlevents(direction_t d);
    void processcurlevents(direction_t d);
    SockInfoMap curlsockets[3];
    m_time_t curltimeoutreset[3];
    bool arerequestspaused[3];
    int numconnections[3];
    set<CURL *>pausedrequests[3];
    m_off_t partialdata[2];
    m_off_t maxspeed[2];

public:
    void post(HttpReq*, const char* = 0, unsigned = 0) override;
    void cancel(HttpReq*) override;

    m_off_t postpos(void*) override;

    bool doio(void) override;
    bool multidoio(CURLM *curlmhandle);

    void measureLatency(CURL* easy_handle, HttpReq* req);

    void addevents(Waiter*, int) override;

    void setuseragent(string*) override;
    void setproxy(const Proxy&) override;
    std::optional<Proxy> getproxy() const override;
    // It returns false if curl does not have a DNS backend supporting custom DNS lists.
    bool setdnsservers(const char*);
    void disconnect() override;

    // set max download speed
    bool setmaxdownloadspeed(m_off_t bpslimit) override;

    // set max upload speed
    bool setmaxuploadspeed(m_off_t bpslimit) override;

    // get max download speed
    m_off_t getmaxdownloadspeed() override;

    // get max upload speed
    m_off_t getmaxuploadspeed() override;

    bool cacheresolvedurls(const std::vector<string>& urls, std::vector<string>&& ips) override;

    CurlHttpIO();
    ~CurlHttpIO();

#ifdef WIN32
    HANDLE mSocketsWaitEvent;
    bool mSocketsWaitEvent_curl_call_needed = false;
#endif

private:
    static int instanceCount;
    friend class MegaClient;
    CodeCounter::ScopeStats countCurlHttpIOAddevents = {"curl-httpio-addevents"};
    CodeCounter::ScopeStats countAddCurlEventsCode = {"curl-add-events"};
    CodeCounter::ScopeStats countProcessCurlEventsCode = {"curl-process-events"};
};

struct MEGA_API CurlHttpContext
{
    CURL* curl;
    direction_t d;

    HttpReq* req;
    CurlHttpIO* httpio;

    struct curl_slist *headers;
    bool isIPv6;
    bool isCachedIp;
    string hostname;
    string scheme;
    int port;
    string hostheader;
    string hostip;
    string posturl;
    unsigned len;
    const char* data;
};

struct MEGA_API CurlDNSEntry
{
    CurlDNSEntry();
    bool isIPv4Expired();
    bool isIPv6Expired();

    string ipv4;
    dstime ipv4timestamp;
    string ipv6;
    dstime ipv6timestamp;

    bool mNeedsResolvingAgain = false;
};

} // namespace
