/**
 * @file mega/posix/meganet.h
 * @brief POSIX network access layer (using cURL)
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

#ifndef HTTPIO_CLASS
#define HTTPIO_CLASS CurlHttpIO

#include "mega.h"
#include <curl/curl.h>
#include <openssl/ssl.h>
#include <ares.h>

namespace mega {

struct MEGA_API CurlHttpContext;
class CurlHttpIO: public HttpIO
{
protected:
    string* useragent;
    CURLM* curlm;
    CURLSH* curlsh;
    ares_channel ares;
    string proxyurl;
    string proxyhost;
    int proxyport;
    string proxyip;
    string proxyusername;
    string proxypassword;
    int proxyinflight;
    time_t ipv6deactivationtime;
    bool ipv6proxyenabled;
    bool ipv6requestsenabled;
    std::queue<CurlHttpContext *> pendingrequests;

    void send_pending_requests();
    void drop_pending_requests();

    static size_t write_data(void*, size_t, size_t, void*);
    static size_t check_header(void*, size_t, size_t, void*);
    static CURLcode ssl_ctx_function(CURL*, void*, void*);
    static int cert_verify_callback(X509_STORE_CTX*, void*);
    static void proxy_ready_callback(void *arg, int status, int timeouts, struct hostent *host);
    static void ares_completed_callback(void *arg, int status, int timeouts, struct hostent *host);
    static void send_request(CurlHttpContext *httpctx);
    void request_proxy_ip();
    static struct curl_slist* clone_curl_slist(struct curl_slist *inlist);
    static bool crackurl(string *url, string *hostname, int* port);
    static int debug_callback(CURL *handle, curl_infotype type, char *data, size_t size, void *userptr);
    bool ipv6available();

    bool curlipv6;
    bool reset;
    string dnsservers;
    curl_slist* contenttypejson;
    curl_slist* contenttypebinary;

#ifndef WINDOWS_PHONE
    PosixWaiter* waiter;
#else
    WinPhoneWaiter* waiter;
#endif

public:
    void post(HttpReq*, const char* = 0, unsigned = 0);
    void cancel(HttpReq*);

    m_off_t postpos(void*);

    bool doio(void);

    void addevents(Waiter*, int);

    void setuseragent(string*);
    void setproxy(Proxy *);
    Proxy *getautoproxy();
	void setdnsservers(const char *dnsservers);

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
    int port;
    string hostheader;
    string hostip;
    unsigned len;
    const char* data;
    int ares_pending;
};

} // namespace

#endif
