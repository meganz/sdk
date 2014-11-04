/**
 * @file posix/net.cpp
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

#include "mega.h"

#define IPV6_RETRY_INTERVAL_DS 72000
#define DNS_CACHE_TIMEOUT_DS 18000

#ifdef WINDOWS_PHONE
const char* inet_ntop(int af, const void* src, char* dst, int cnt)
{
    struct sockaddr_in srcaddr;
    wchar_t ip[INET6_ADDRSTRLEN];
    int len = INET6_ADDRSTRLEN;

    memset(&srcaddr, 0, sizeof(struct sockaddr_in));
    memcpy(&(srcaddr.sin_addr), src, sizeof(srcaddr.sin_addr));

    srcaddr.sin_family = af;

    if (WSAAddressToString((struct sockaddr*) &srcaddr, sizeof(struct sockaddr_in), 0, ip, (LPDWORD)&len) != 0) 
    {
        return NULL;
    }

    if (!WideCharToMultiByte(CP_UTF8, 0, ip, len, dst, cnt, NULL, NULL))
    {
        return NULL;
    }

    return dst;
}
#else
#include <netdb.h>
#endif

namespace mega {

CurlHttpIO::CurlHttpIO()
{
    curl_version_info_data* data = curl_version_info(CURLVERSION_NOW);
    string curlssl = data->ssl_version;
    std::transform(curlssl.begin(), curlssl.end(), curlssl.begin(), ::tolower);

    if (!strstr(curlssl.c_str(), "openssl"))
    {
        LOG_fatal << "cURL built without OpenSSL support. Aborting.";
        exit(EXIT_FAILURE);
    }

    int i;

    for (i = 0; data->protocols[i]; i++)
    {
        if (strstr(data->protocols[i], "http"))
        {
            break;
        }
    }

    if (!data->protocols[i] || !(data->features & CURL_VERSION_SSL))
    {
        LOG_fatal << "cURL built without HTTP/HTTPS support. Aborting.";
        exit(EXIT_FAILURE);
    }

    curlipv6 = data->features & CURL_VERSION_IPV6;
    reset = false;
    statechange = false;

    WAIT_CLASS::bumpds();
    lastdnspurge = Waiter::ds + DNS_CACHE_TIMEOUT_DS / 2;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    ares_library_init(ARES_LIB_INIT_ALL);

    curlm = curl_multi_init();
    ares_init(&ares);

    curl_multi_setopt(curlm, CURLMOPT_MAXCONNECTS, 256);

    curlsh = curl_share_init();
    curl_share_setopt(curlsh, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(curlsh, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);

    contenttypejson = curl_slist_append(NULL, "Content-Type: application/json");
    contenttypejson = curl_slist_append(contenttypejson, "Expect:");

    contenttypebinary = curl_slist_append(NULL, "Content-Type: application/octet-stream");
    contenttypebinary = curl_slist_append(contenttypebinary, "Expect:");

    proxyinflight = 0;
    ipv6requestsenabled = ipv6available();
    ipv6proxyenabled = ipv6requestsenabled;
    ipv6deactivationtime = 0;
}

bool CurlHttpIO::ipv6available()
{
    static int ipv6_works = -1;

    if (ipv6_works != -1)
    {
        return ipv6_works;
    }

    int s = socket(PF_INET6, SOCK_DGRAM, 0);

    if (s == -1)
    {
        ipv6_works = 0;
    }
    else
    {
        ipv6_works = curlipv6;
#ifdef WINDOWS_PHONE
		closesocket(s);
#else
        close(s);
#endif
    }

    return ipv6_works;
}

CurlHttpIO::~CurlHttpIO()
{
    curl_multi_cleanup(curlm);
    ares_destroy(ares);

    curl_global_cleanup();
    ares_library_cleanup();
}

void CurlHttpIO::setuseragent(string* u)
{
    useragent = u;
}

void CurlHttpIO::setdnsservers(const char* servers)
{
    if (servers)
    {
        lastdnspurge = Waiter::ds + DNS_CACHE_TIMEOUT_DS / 2;
        dnscache.clear();

        dnsservers = servers;
        ares_set_servers_csv(ares, servers);
    }
}

// wake up from cURL I/O
void CurlHttpIO::addevents(Waiter* w, int)
{
    int t;

    waiter = (WAIT_CLASS*)w;
    curl_multi_fdset(curlm, &waiter->rfds, &waiter->wfds, &waiter->efds, &t);
    waiter->bumpmaxfd(t);

    long curltimeout;
    
    curl_multi_timeout(curlm, &curltimeout);

    if (curltimeout >= 0)
    {
        curltimeout /= 100;
        if ((unsigned long)curltimeout < waiter->maxds)
        waiter->maxds = curltimeout;
    }

    t = ares_fds(ares, &waiter->rfds, &waiter->wfds);
    waiter->bumpmaxfd(t);

    timeval tv;

    if (ares_timeout(ares, NULL, &tv))
    {
        dstime arestimeout = tv.tv_sec * 10 + tv.tv_usec / 100000;

        if (arestimeout < waiter->maxds)
        {
            waiter->maxds = arestimeout;
        }
    }
}

void CurlHttpIO::proxy_ready_callback(void* arg, int status, int, hostent* host)
{
    // the name of a proxy has been resolved
    CurlHttpContext* httpctx = (CurlHttpContext*)arg;
    CurlHttpIO* httpio = httpctx->httpio;

    httpctx->ares_pending--;

    if (!httpctx->ares_pending)
    {
        httpio->proxyinflight--;
    }

    if (!httpio->proxyhost.size() // the proxy was disabled during the name resolution.
     || httpio->proxyip.size())   // or we already have the correct ip
    {
        if (!httpctx->ares_pending)
        {
            // name resolution finished.
            // nothing more to do.
            // free resources and continue sending requests.
            delete httpctx;
            httpio->send_pending_requests();
        }

        return;
    }

    // check if result is valid
    // IPv6 takes precedence over IPv4
    // discard the IP if it's IPv6 and IPv6 isn't available
    if (status == ARES_SUCCESS && host && host->h_addr_list[0]
     && httpio->proxyhost == httpctx->hostname
     && (!httpctx->hostip.size() || host->h_addrtype == PF_INET6)
     && (host->h_addrtype != PF_INET6 || httpio->ipv6available()))
    {
        // save the IP of the proxy
        char ip[INET6_ADDRSTRLEN];

        inet_ntop(host->h_addrtype, host->h_addr_list[0], ip, sizeof ip);
        httpctx->hostip = ip;
        httpctx->isIPv6 = host->h_addrtype == PF_INET6;

        if (httpctx->isIPv6 && ip[0] != '[')
        {
            httpctx->hostip.insert(0, "[");
            httpctx->hostip.append("]");
        }
    }

    if (!httpctx->ares_pending)
    {
        // name resolution finished
        // if the IP is valid, use it and continue sending requests.
        if (httpio->proxyhost == httpctx->hostname && httpctx->hostip.size())
        {
            std::ostringstream oss;
            
            oss << httpctx->hostip << ":" << httpio->proxyport;
            httpio->proxyip = oss.str();

            LOG_info << "Updated proxy URL: " << httpio->proxyip;

            httpio->send_pending_requests();
        }
        else if (!httpio->proxyinflight)
        {
            httpio->inetstatus(false);

            // the IP isn't up to date and there aren't pending
            // name resolutions for proxies. Abort requests.
            httpio->drop_pending_requests();

            // reinitialize c-ares to prevent persistent hangs
            httpio->reset = true;
        }

        // nothing more to do - free resources
        delete httpctx;
    }
}

void CurlHttpIO::ares_completed_callback(void* arg, int status, int, struct hostent* host)
{
    CurlHttpContext* httpctx = (CurlHttpContext*)arg;
    CurlHttpIO* httpio = httpctx->httpio;
    HttpReq* req = httpctx->req;
    httpctx->ares_pending--;

    if (!req) // the request was cancelled
    {
        if (!httpctx->ares_pending)
        {
            delete httpctx;
        }

        return;
    }

    // check if result is valid
    if (status == ARES_SUCCESS && host && host->h_addr_list[0])
    {
        char ip[INET6_ADDRSTRLEN];

        inet_ntop(host->h_addrtype, host->h_addr_list[0], ip, sizeof(ip));

        // add to DNS cache
        CurlDNSEntry& dnsEntry = httpio->dnscache[httpctx->hostname];

        if (host->h_addrtype == PF_INET6)
        {
            dnsEntry.ipv6 = ip;
            dnsEntry.ipv6timestamp = Waiter::ds;
        }
        else
        {
            dnsEntry.ipv4 = ip;
            dnsEntry.ipv4timestamp = Waiter::ds;
        }

        // IPv6 takes precedence over IPv4
        if (!httpctx->hostip.size() || host->h_addrtype == PF_INET6)
        {
            httpctx->isIPv6 = host->h_addrtype == PF_INET6;

            //save the IP for this request
            std::ostringstream oss;
            if (httpctx->isIPv6)
            {
                oss << "[" << ip << "]:" << httpctx->port;
            }
            else
            {
                oss << ip << ":" << httpctx->port;
            }

            httpctx->hostip = oss.str();
        }
    }

    if (!httpctx->ares_pending)
    {
        // name resolution finished
        // check for fatal errors
        if ((httpio->proxyurl.size() && !httpio->proxyhost.size()) //malformed proxy string
         || !httpctx->hostip.size()) // or unable to get the IP for this request
        {
            req->status = REQ_FAILURE;
            httpio->statechange = true;

            if (!httpctx->hostip.size())
            {
                // unable to get the IP.
                httpio->inetstatus(false);

                // reinitialize c-ares to prevent permanent hangs
                httpio->reset = true;
            }

            return;
        }

        // if there is no proxy or we already have the IP of the proxy, send the request.
        // otherwise, queue the request until we get the IP of the proxy
        if (!httpio->proxyurl.size() || httpio->proxyip.size())
        {
            send_request(httpctx);
        }
        else
        {
            httpio->pendingrequests.push(httpctx);

            if (!httpio->proxyinflight)
            {
                // c-ares failed to get the IP of the proxy.
                // queue this request and retry.
                httpio->ipv6proxyenabled = !httpio->ipv6proxyenabled && httpio->ipv6available();
                httpio->request_proxy_ip();
                return;
            }
        }
    }
}

struct curl_slist* CurlHttpIO::clone_curl_slist(struct curl_slist* inlist)
{
    struct curl_slist* outlist = NULL;
    struct curl_slist* tmp;

    while (inlist)
    {
        tmp = curl_slist_append(outlist, inlist->data);

        if (!tmp)
        {
            curl_slist_free_all(outlist);
            return NULL;
        }

        outlist = tmp;
        inlist = inlist->next;
    }

    return outlist;
}

void CurlHttpIO::send_request(CurlHttpContext* httpctx)
{
    CurlHttpIO* httpio = httpctx->httpio;
    HttpReq* req = httpctx->req;
    int len = httpctx->len;
    const char* data = httpctx->data;

    LOG_debug << "POST target URL: " << req->posturl;
    if (req->binary)
    {
        LOG_debug << "[sending " << (data ? len : req->out->size()) << " bytes of raw data]";
    }
    else
    {
        LOG_debug << "Sending: " << *req->out;
    }

    req->posturl.replace(req->posturl.find(httpctx->hostname), httpctx->hostname.size(), httpctx->hostip);
    httpctx->headers = clone_curl_slist(req->type == REQ_JSON ? httpio->contenttypejson : httpio->contenttypebinary);
    httpctx->headers = curl_slist_append(httpctx->headers, httpctx->hostheader.c_str());

    CURL* curl;

    if ((curl = curl_easy_init()))
    {
        curl_easy_setopt(curl, CURLOPT_POST, 1);
        curl_easy_setopt(curl, CURLOPT_URL, req->posturl.c_str());
        
        if (req->chunked)
        {
            curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_data);
            curl_easy_setopt(curl, CURLOPT_READDATA, (void*)req);                     
            curl_slist_append(httpctx->headers, "Transfer-Encoding: chunked");
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data ? data : req->out->data());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data ? len : req->out->size());
        }

        curl_easy_setopt(curl, CURLOPT_USERAGENT, httpio->useragent->c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, httpctx->headers);
        curl_easy_setopt(curl, CURLOPT_ENCODING, "");
        curl_easy_setopt(curl, CURLOPT_SHARE, httpio->curlsh);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)req);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, check_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void*)req);
        curl_easy_setopt(curl, CURLOPT_PRIVATE, (void*)req);
        curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, ssl_ctx_function);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
        curl_easy_setopt(curl, CURLOPT_CAINFO, NULL);
        curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);

        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, debug_callback);
        curl_easy_setopt(curl, CURLOPT_DEBUGDATA, (void*)req);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

        if (httpio->proxyip.size())
        {
            curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);

            curl_easy_setopt(curl, CURLOPT_PROXY, httpio->proxyip.c_str());
            curl_easy_setopt(curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);

            if (httpio->proxyusername.size())
            {
                curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME, httpio->proxyusername.c_str());
                curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD, httpio->proxypassword.c_str());
            }

            if(httpctx->port == 443)
            {
                curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
            }
        }

        curl_multi_add_handle(httpio->curlm, curl);

        httpctx->curl = curl;
    }
    else
    {
        req->status = REQ_FAILURE;
    }

    httpio->statechange = true;
}

void CurlHttpIO::request_proxy_ip()
{
    if (!proxyhost.size())
    {
        return;
    }

    proxyinflight++;

    proxyip.clear();

    CurlHttpContext* httpctx = new CurlHttpContext;
    httpctx->httpio = this;
    httpctx->hostname = proxyhost;
    httpctx->ares_pending = 1;

    if (ipv6proxyenabled)
    {
        httpctx->ares_pending++;
        ares_gethostbyname(ares, proxyhost.c_str(), PF_INET6, proxy_ready_callback, httpctx);
    }

    ares_gethostbyname(ares, proxyhost.c_str(), PF_INET, proxy_ready_callback, httpctx);
}

bool CurlHttpIO::crackurl(string* url, string* hostname, int* port)
{
    *port = 0;

    hostname->clear();

    if (!url || !url->size() || !hostname || !port)
    {
        return false;
    }

    size_t starthost, endhost, startport, endport;

    starthost = url->find("://");

    if (starthost != string::npos)
    {
        starthost += 3;
    }
    else
    {
        starthost = 0;
    }

    if ((*url)[starthost] == '[' && url->size() > 0)
    {
        starthost++;
    }

    startport = url->find("]:", starthost);

    if (startport == string::npos)
    {
        startport = url->find(":", starthost);

        if (startport != string::npos)
        {
            endhost = startport;
        }
    }
    else
    {
        endhost = startport;
        startport++;
    }

    if (startport != string::npos)
    {
        startport++;

        endport = url->find("/", startport);

        if (endport == string::npos)
        {
            endport = url->size();
        }

        if (endport <= startport || endport - startport > 5)
        {
            *port = -1;
        }
        else
        {
            for (unsigned int i = startport; i < endport; i++)
            {
                int c = url->data()[i];

                if (c < '0' || c > '9')
                {
                    *port = -1;
                    break;
                }
            }
        }

        if (!*port)
        {
            *port = atoi(url->data() + startport);

            if (*port > 65535)
            {
                *port = -1;
            }
        }
    }
    else
    {
        endhost = url->find("]/", starthost);

        if (endhost == string::npos)
        {
            endhost = url->find("/", starthost);

            if (endhost == string::npos)
            {
                endhost = url->size();
            }
        }
    }

    if (!*port)
    {
        if (!url->compare(0, 8, "https://"))
        {
            *port = 443;
        }
        else if (!url->compare(0, 7, "http://"))
        {
            *port = 80;
        }
        else
        {
            *port = -1;
        }
    }

    *hostname = url->substr(starthost, endhost - starthost);

    if (*port <= 0 || starthost == string::npos || starthost >= endhost)
    {
        return false;
    }

    return true;
}

int CurlHttpIO::debug_callback(CURL*, curl_infotype type, char* data, size_t size, void*)
{
    if(type == CURLINFO_TEXT && size)
    {
        data[size-1] = 0;
        LOG_verbose << "cURL DEBUG: " << data;
    }

    return 0;
}

// POST request to URL
void CurlHttpIO::post(HttpReq* req, const char* data, unsigned len)
{
    CurlHttpContext* httpctx = new CurlHttpContext;
    httpctx->curl = NULL;
    httpctx->httpio = this;
    httpctx->req = req;
    httpctx->len = len;
    httpctx->data = data;
    httpctx->headers = NULL;
    httpctx->ares_pending = 0;

    req->outbuf.append(req->chunkedout);
    req->chunkedout.clear();

    req->httpiohandle = (void*)httpctx;

    if ((proxyurl.size() && !proxyhost.size()) // malformed proxy string
     || !crackurl(&req->posturl, &httpctx->hostname, &httpctx->port)) // invalid request
    {
        req->status = REQ_FAILURE;
        statechange = true;
        return;
    }

    if (!ipv6requestsenabled && ipv6available())
    {
        if ((Waiter::ds - ipv6deactivationtime) > IPV6_RETRY_INTERVAL_DS)
        {
            ipv6requestsenabled = true;
        }
    }

    if (reset)
    {
        reset = false;
        ares_destroy(ares);
        ares_init(&ares);

        if (dnsservers.size())
        {
            ares_set_servers_csv(ares, dnsservers.c_str());
        }
    }

    // purge DNS cache if needed
    if (Waiter::ds - lastdnspurge > DNS_CACHE_TIMEOUT_DS)
    {
        std::map<string, CurlDNSEntry>::iterator it = dnscache.begin();

        while (it != dnscache.end())
        {
            CurlDNSEntry& entry = it->second;

            if (entry.ipv6.size() && Waiter::ds - entry.ipv6timestamp >= DNS_CACHE_TIMEOUT_DS)
            {
                entry.ipv6timestamp = 0;
                entry.ipv6.clear();
            }

            if (entry.ipv4.size() && Waiter::ds - entry.ipv4timestamp >= DNS_CACHE_TIMEOUT_DS)
            {
                entry.ipv4timestamp = 0;
                entry.ipv4.clear();
            }

            if (!entry.ipv6.size() && !entry.ipv4.size())
            {
                dnscache.erase(it++);
            }
            else
            {
                it++;
            }
        }

        lastdnspurge = Waiter::ds;
    }

    req->in.clear();
    req->status = REQ_INFLIGHT;
    httpctx->hostheader = "Host: ";
    httpctx->hostheader.append(httpctx->hostname);
    httpctx->ares_pending = 1;

    CurlDNSEntry& dnsEntry = dnscache[httpctx->hostname];

    if (ipv6requestsenabled)
    {
        if (dnsEntry.ipv6.size() && Waiter::ds - dnsEntry.ipv6timestamp < DNS_CACHE_TIMEOUT_DS)
        {
            std::ostringstream oss;
            httpctx->isIPv6 = true;
            oss << "[" << dnsEntry.ipv6 << "]:" << httpctx->port;
            httpctx->hostip = oss.str();
            httpctx->ares_pending = 0;
            send_request(httpctx);
            return;
        }

        httpctx->ares_pending++;
        ares_gethostbyname(ares, httpctx->hostname.c_str(), PF_INET6, ares_completed_callback, httpctx);
    }
    else
    {
        if (dnsEntry.ipv4.size() && Waiter::ds - dnsEntry.ipv4timestamp < DNS_CACHE_TIMEOUT_DS)
        {
            std::ostringstream oss;
            httpctx->isIPv6 = false;
            oss << dnsEntry.ipv4 << ":" << httpctx->port;
            httpctx->hostip = oss.str();
            httpctx->ares_pending = 0;
            send_request(httpctx);
            return;
        }
    }

    ares_gethostbyname(ares, httpctx->hostname.c_str(), PF_INET, ares_completed_callback, httpctx);
}

void CurlHttpIO::setproxy(Proxy* proxy)
{
    // clear the previous proxy IP
    proxyip.clear();

    if (proxy->getProxyType() != Proxy::CUSTOM || !proxy->getProxyURL().size())
    {
        // automatic proxy is not supported
        // invalidate inflight proxy changes
        proxyhost.clear();

        // don't use a proxy
        proxyurl.clear();

        // send pending requests without a proxy
        send_pending_requests();
        return;
    }

    proxyurl = proxy->getProxyURL();
    proxyusername = proxy->getUsername();
    proxypassword = proxy->getPassword();

    if (!crackurl(&proxyurl, &proxyhost, &proxyport))
    {
        // malformed proxy string
        // invalidate inflight proxy changes

        // mark the proxy as invalid (proxyurl set but proxyhost not set)
        proxyhost.clear();

        // drop all pending requests
        drop_pending_requests();
        return;
    }

    ipv6requestsenabled = ipv6available();
    ipv6proxyenabled = ipv6requestsenabled;
    request_proxy_ip();
}

Proxy* CurlHttpIO::getautoproxy()
{
    Proxy* proxy = new Proxy();
    proxy->setProxyType(Proxy::NONE);
    return proxy;
}

// cancel pending HTTP request
void CurlHttpIO::cancel(HttpReq* req)
{
    if (req->httpiohandle)
    {
        CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;

        if (httpctx->curl)
        {
            curl_multi_remove_handle(curlm, httpctx->curl);
            curl_easy_cleanup(httpctx->curl);
            curl_slist_free_all(httpctx->headers);
        }

        httpctx->req = NULL;

        if (req->status == REQ_FAILURE || httpctx->curl)
        {
            delete httpctx;
        }

        req->httpstatus = 0;

        if (req->status != REQ_FAILURE)
        {
            req->status = REQ_FAILURE;
            statechange = true;
        }

        req->httpiohandle = NULL;
    }
}

// real-time progress information on POST data
m_off_t CurlHttpIO::postpos(void* handle)
{
    double bytes = 0;

    CurlHttpContext* httpctx = (CurlHttpContext*)handle;

    if (httpctx->curl)
    {
        curl_easy_getinfo(httpctx->curl, CURLINFO_SIZE_UPLOAD, &bytes);
    }

    return (m_off_t)bytes;
}

// process events
bool CurlHttpIO::doio()
{
    bool result;
    CURLMsg* msg;
    int dummy;

    ares_process(ares, &waiter->rfds, &waiter->wfds);
    curl_multi_perform(curlm, &dummy);

    while ((msg = curl_multi_info_read(curlm, &dummy)))
    {
        HttpReq* req = NULL;

        if (curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (char**)&req) == CURLE_OK && req)
        {
            req->httpio = NULL;

            if (msg->msg == CURLMSG_DONE)
            {
                curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &req->httpstatus);

                LOG_debug << "CURLMSG_DONE with HTTP status: " << req->httpstatus;
                if (req->httpstatus)
                {
                    if (req->binary)
                    {
                        LOG_debug << "[received " << req->in.size() << " bytes of raw data]";
                    }
                    else
                    {
                        LOG_debug << "Received: " << req->in.c_str();
                    }
                }

                // check httpstatus and response length
                req->status = (req->httpstatus == 200
                            && (req->contentlength < 0
                             || req->contentlength == (req->buf ? req->bufpos : (int)req->in.size())))
                             ? REQ_SUCCESS : REQ_FAILURE;

                if (req->status == REQ_SUCCESS)
                {
                    lastdata = Waiter::ds;
                }

                success = true;
            }
            else
            {
                req->status = REQ_FAILURE;
            }

            statechange = true;

            if (req->status == REQ_FAILURE && !req->httpstatus)
            {                
                CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;

                // remove the IP from the DNS cache
                CurlDNSEntry &dnsEntry = dnscache[httpctx->hostname];

                if (httpctx->isIPv6)
                {
                    dnsEntry.ipv6.clear();
                    dnsEntry.ipv6timestamp = 0;
                }
                else
                {
                    dnsEntry.ipv4.clear();
                    dnsEntry.ipv4timestamp = 0;
                }

                ipv6requestsenabled = !httpctx->isIPv6 && ipv6available();

                if (ipv6requestsenabled)
                {
                    // change the protocol of the proxy after fails contacting
                    // MEGA servers with both protocols (IPv4 and IPv6)
                    ipv6proxyenabled = !ipv6proxyenabled && ipv6available();
                    request_proxy_ip();
                }
                else if (httpctx->isIPv6)
                {
                    ipv6deactivationtime = Waiter::ds;

                    // for IPv6 errors, try IPv4 before sending an error to the engine
                    if (dnsEntry.ipv4.size() && Waiter::ds - dnsEntry.ipv4timestamp < DNS_CACHE_TIMEOUT_DS)
                    {
                        curl_multi_remove_handle(curlm, msg->easy_handle);
                        curl_easy_cleanup(msg->easy_handle);
                        curl_slist_free_all(httpctx->headers);

                        req->httpio = this;
                        req->in.clear();
                        req->posturl.replace(req->posturl.find(httpctx->hostip), httpctx->hostip.size(), httpctx->hostname);
                        req->status = REQ_INFLIGHT;
                        httpctx->ares_pending = 0;
                        httpctx->isIPv6 = false;
                        std::ostringstream oss;
                        oss << dnsEntry.ipv4 << ":" << httpctx->port;
                        httpctx->hostip = oss.str();
                        send_request(httpctx);
                        return true;
                    }
                }
            }
        }
        else
        {
            req = NULL;
        }

        curl_multi_remove_handle(curlm, msg->easy_handle);
        curl_easy_cleanup(msg->easy_handle);

        if (req)
        {
            inetstatus(req->status);

            CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;
            curl_slist_free_all(httpctx->headers);
            delete httpctx;
            req->httpiohandle = NULL;
        }
    }

    result = statechange;
    statechange = false;
    return result;
}

// callback for incoming HTTP payload
void CurlHttpIO::send_pending_requests()
{
    while (pendingrequests.size())
    {
        CurlHttpContext* httpctx = pendingrequests.front();

        if (httpctx->req)
        {
            send_request(httpctx);
        }
        else
        {
            delete httpctx;
        }

        pendingrequests.pop();
    }
}

void CurlHttpIO::drop_pending_requests()
{
    while (pendingrequests.size())
    {
        CurlHttpContext* httpctx = pendingrequests.front();

        if (httpctx->req)
        {
            httpctx->req->status = REQ_FAILURE;
            statechange = true;
        }
        else
        {
            delete httpctx;
        }

        pendingrequests.pop();
    }
}

// unpause potentially paused connection after more data was added to req->out, calling read_data() again
void CurlHttpIO::sendchunked(HttpReq* req)
{
    if (req->httpiohandle)
    {
        CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;

        if (httpctx->curl)
        {
            curl_easy_pause(httpctx->curl, CURLPAUSE_CONT);
        }
    }
}

size_t CurlHttpIO::read_data(void* ptr, size_t size, size_t nmemb, void* source)
{
    if (!((HttpReq*)source)->out)
    {
        return 0;
    }

    curl_off_t nread = ((HttpReq*)source)->out->size();
    
    if (nread > nmemb)
    {
        nread = nmemb;
    }
    
    if (!nread)
    {
        return CURL_READFUNC_PAUSE;
    }
    
    memcpy(ptr, ((HttpReq*)source)->out->data(), nread);
    ((HttpReq*)source)->out->erase(0, nread);
    
    return nread;
}

size_t CurlHttpIO::write_data(void* ptr, size_t, size_t nmemb, void* target)
{
    if (((HttpReq*)target)->chunked)
    {
        ((CurlHttpIO*)((HttpReq*)target)->httpio)->statechange = true;
    }

    ((HttpReq*)target)->put(ptr, nmemb, true);
    ((HttpReq*)target)->httpio->lastdata = Waiter::ds;

    return nmemb;
}

// set contentlength according to Original-Content-Length header
size_t CurlHttpIO::check_header(void* ptr, size_t, size_t nmemb, void* target)
{
    if (!memcmp(ptr, "Content-Length:", 15))
    {
        if (((HttpReq*)target)->contentlength < 0) ((HttpReq*)target)->setcontentlength(atol((char*)ptr + 15));
    }
    else
    {
        if (!memcmp(ptr, "Original-Content-Length:", 24))
        {
            ((HttpReq*)target)->setcontentlength(atol((char*)ptr + 24));
        }
    }

    if (((HttpReq*)target)->httpio)
    {
        ((HttpReq*)target)->httpio->lastdata = Waiter::ds;
    }

    return nmemb;
}

CURLcode CurlHttpIO::ssl_ctx_function(CURL*, void* sslctx, void*)
{
    SSL_CTX_set_cert_verify_callback((SSL_CTX*)sslctx, cert_verify_callback, NULL);

    return CURLE_OK;
}

// SSL public key pinning
int CurlHttpIO::cert_verify_callback(X509_STORE_CTX* ctx, void*)
{
    unsigned char buf[sizeof(APISSLMODULUS1) - 1];
    EVP_PKEY* evp;
    int ok = 0;

    if ((evp = X509_PUBKEY_get(X509_get_X509_PUBKEY(ctx->cert))))
    {
        if (BN_num_bytes(evp->pkey.rsa->n) == sizeof APISSLMODULUS1 - 1
         && BN_num_bytes(evp->pkey.rsa->e) == sizeof APISSLEXPONENT - 1)
        {
            BN_bn2bin(evp->pkey.rsa->n, buf);

            if (!memcmp(buf, APISSLMODULUS1, sizeof APISSLMODULUS1 - 1) || !memcmp(buf, APISSLMODULUS2, sizeof APISSLMODULUS2 - 1))
            {
                BN_bn2bin(evp->pkey.rsa->e, buf);

                if (!memcmp(buf, APISSLEXPONENT, sizeof APISSLEXPONENT - 1))
                {
                    ok = 1;
                }
            }
        }

        EVP_PKEY_free(evp);
    }

    return ok;
}

CurlDNSEntry::CurlDNSEntry()
{
    ipv4timestamp = 0;
    ipv6timestamp = 0;
}

} // namespace
