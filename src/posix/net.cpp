/**
 * @file posix/net.cpp
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

#include "mega.h"

#define IPV6_RETRY_INTERVAL_SECS 7200

#ifdef WINDOWS_PHONE
const char* inet_ntop(int af, const void* src, char* dst, int cnt){

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
    curl_version_info_data *data = curl_version_info(CURLVERSION_NOW);
    string curlssl = data->ssl_version;
    std::transform(curlssl.begin(), curlssl.end(), curlssl.begin(), ::tolower);
    if(!strstr(curlssl.c_str(), "openssl"))
    {
        cerr << "cURL built without OpenSSL support. Aborting." << endl;
        exit(EXIT_FAILURE);
    }

    bool http = false;
    int i = 0;
    while(data->protocols[i])
    {
        if(strstr(data->protocols[i], "http"))
        {
            http = true;
            break;
        }
        i++;
    }

    if(!http || !(data->features & CURL_VERSION_SSL))
    {
        cerr << "cURL built without HTTP/HTTPS support. Aborting." << endl;
        exit(EXIT_FAILURE);
    }

    curlipv6 = data->features & CURL_VERSION_IPV6;

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
    if(ipv6_works != -1)
        return ipv6_works;

    int s = socket(PF_INET6, SOCK_DGRAM, 0);
    if(s == -1)
    {
        ipv6_works = 0;
    }
    else
    {
        ipv6_works = curlipv6;
        close(s);
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
		ares_set_servers_csv(ares, servers);
    }
}

// wake up from cURL I/O
void CurlHttpIO::addevents(Waiter* w, int)
{
    int t;

#ifndef WINDOWS_PHONE
    waiter = (PosixWaiter* )w;
#else
    waiter = (WinPhoneWaiter* )w;
#endif

    curl_multi_fdset(curlm, &waiter->rfds, &waiter->wfds, &waiter->efds, &t);
    waiter->bumpmaxfd(t);

    t = ares_fds(ares, &waiter->rfds, &waiter->wfds);
    waiter->bumpmaxfd(t);
}

void CurlHttpIO::proxy_ready_callback(void *arg, int status, int, hostent *host)
{
    //the name of a proxy has been resolved
    CurlHttpContext *httpctx = (CurlHttpContext *)arg;
    CurlHttpIO *httpio = httpctx->httpio;

    httpctx->ares_pending--;
    if(!httpctx->ares_pending)
    {
        httpio->proxyinflight--;
    }

    if(!httpio->proxyhost.size() || //the proxy was disabled during the name resolution.
        httpio->proxyip.size())     //or we already have the correct ip
    {
        if(!httpctx->ares_pending)
        {
            //Name resolution finished.
            //Nothing more to do.
            //Free resources and continue sending requests.
            delete httpctx;
            httpio->send_pending_requests();
        }

        return;
    }

    //Check if result is valid
    //IPv6 takes precedence over IPv4
    //Discard the IP if it's IPv6 and IPv6 isn't available
    if(!(status != ARES_SUCCESS || !host || !host->h_addr_list[0]) &&
        (httpio->proxyhost == httpctx->hostname) &&
        (!httpctx->hostip.size() || host->h_addrtype == PF_INET6) &&
        (!(host->h_addrtype == PF_INET6) || httpio->ipv6available()))
    {
        //Save the IP of the proxy
        char ip[INET6_ADDRSTRLEN];
        inet_ntop(host->h_addrtype, host->h_addr_list[0], ip, sizeof(ip));
        httpctx->hostip = ip;
        httpctx->isIPv6 = (host->h_addrtype == PF_INET6);
        if(httpctx->isIPv6 && ip[0] != '[')
        {
            httpctx->hostip.insert(0, "[");
            httpctx->hostip.append("]");
        }
    }

    if(!httpctx->ares_pending)
    {
        //Name resolution finished

        //If the IP is valid, use it and continue sending requests.
        if((httpio->proxyhost == httpctx->hostname) && httpctx->hostip.size())
        {
            std::ostringstream oss;
            oss << httpctx->hostip << ":" << httpio->proxyport;
            httpio->proxyip = oss.str();

            if(debug)
            {
                cout << "Updated proxy URL: " << httpio->proxyip << endl;
            }

            httpio->send_pending_requests();
        }
        else if(!httpio->proxyinflight)
        {
            //The IP isn't up to date and there aren't pending
            //name resolutions for proxies. Abort requests.
            httpio->drop_pending_requests();
        }

        //Nothing more to do.
        //Free resources.
        delete httpctx;
    }
}

void CurlHttpIO::ares_completed_callback(void *arg, int status, int, struct hostent *host)
{
    CurlHttpContext* httpctx = (CurlHttpContext *)arg;
    CurlHttpIO *httpio = httpctx->httpio;
    HttpReq* req = httpctx->req;
    httpctx->ares_pending--;

    if(!req) //The request was cancelled
    {
        if(!httpctx->ares_pending)
        {
            delete httpctx;
        }

        return;
    }

    //Check if result is valid
    //IPv6 takes precedence over IPv4
    //Discard the IP if it's IPv6 and IPv6 isn't available
    if(!(status != ARES_SUCCESS || !host || !host->h_addr_list[0]) &&
       (!httpctx->hostip.size() || host->h_addrtype == PF_INET6) &&
       (!(host->h_addrtype == PF_INET6) || httpio->ipv6available()))
    {
        //save the IP for this request
        char ip[INET6_ADDRSTRLEN];
        inet_ntop(host->h_addrtype, host->h_addr_list[0], ip, sizeof(ip));
        httpctx->hostheader = "Host: ";
        httpctx->hostheader.append(httpctx->hostname);

        httpctx->isIPv6 = (host->h_addrtype == PF_INET6);
        std::ostringstream oss;
        if(httpctx->isIPv6 && ip[0] != '[')
        {
            oss << "[" << ip << "]:" << httpctx->port;
        }
        else
        {
            oss << ip << ":" << httpctx->port;
        }

        httpctx->hostip = oss.str();
    }

    if(!httpctx->ares_pending)
    {
        //Name resolution finished

        //Check for fatal errors
        if((httpio->proxyurl.size() && !httpio->proxyhost.size()) || //malformed proxy string
          (!httpctx->hostip.size())) //or unable to get the IP for this request
        {
            req->status = REQ_FAILURE;
            return;
        }

        //If there is no proxy or we already have the IP of the proxy, send the request.
        //Otherwise, queue the request until we get the IP of the proxy
        if(!httpio->proxyurl.size() || httpio->proxyip.size())
        {
            send_request(httpctx);
        }
        else
        {
            httpio->pendingrequests.push(httpctx);
            if(!httpio->proxyinflight)
            {
                //c-ares failed getting the IP of the proxy.
                //Queue this request and retry.
                httpio->ipv6proxyenabled = !httpio->ipv6proxyenabled && httpio->ipv6available();
                httpio->request_proxy_ip();
                return;
            }
        }
    }
}

struct curl_slist* CurlHttpIO::clone_curl_slist(struct curl_slist *inlist)
{
    struct curl_slist *outlist = NULL;
    struct curl_slist *tmp;

    while(inlist)
    {
        tmp = curl_slist_append(outlist, inlist->data);

        if(!tmp)
        {
            curl_slist_free_all(outlist);
            return NULL;
        }

        outlist = tmp;
        inlist = inlist->next;
    }

    return outlist;
}

void CurlHttpIO::send_request(CurlHttpContext *httpctx)
{
    CurlHttpIO *httpio = httpctx->httpio;
    HttpReq* req = httpctx->req;
    int len = httpctx->len;
    const char* data = httpctx->data;

    if (debug)
    {
        cout << "POST target URL: " << req->posturl << endl;

        if (req->binary)
        {
            cout << "[sending " << (data ? len : req->out->size()) << " bytes of raw data]" << endl;
        }
        else
        {
            cout << "Sending: " << *req->out << endl;
        }
    }

    req->posturl.replace(req->posturl.find(httpctx->hostname), httpctx->hostname.size(), httpctx->hostip);
    httpctx->headers = clone_curl_slist(req->type == REQ_JSON ? httpio->contenttypejson : httpio->contenttypebinary);
    httpctx->headers = curl_slist_append(httpctx->headers, httpctx->hostheader.c_str());

    CURL* curl;
    if ((curl = curl_easy_init()))
    {
        curl_easy_setopt(curl, CURLOPT_URL, req->posturl.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data ? data : req->out->data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data ? len : req->out->size());
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

        if(debug)
        {
            curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, debug_callback);
            curl_easy_setopt(curl, CURLOPT_DEBUGDATA, (void*)req);
            curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
        }

        if(httpio->proxyip.size())
        {
            curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);

            curl_easy_setopt(curl, CURLOPT_PROXY, httpio->proxyip.c_str());
            curl_easy_setopt(curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);

            if(httpio->proxyusername.size())
            {
                curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME, httpio->proxyusername.c_str());
                curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD, httpio->proxypassword.c_str());
            }

            curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
        }

        curl_multi_add_handle(httpio->curlm, curl);

        httpctx->curl = curl;
    }
    else
    {
        req->status = REQ_FAILURE;
    }
}

void CurlHttpIO::request_proxy_ip()
{
    if(!proxyhost.size())
    {
        return;
    }

    proxyinflight++;

    proxyip.clear();

    CurlHttpContext *httpctx = new CurlHttpContext;
    httpctx->httpio = this;
    httpctx->hostname = proxyhost;
    httpctx->ares_pending = 1;

    if(ipv6proxyenabled)
    {
        httpctx->ares_pending++;
        ares_gethostbyname(ares, proxyhost.c_str(), PF_INET6, proxy_ready_callback, httpctx);
    }
    ares_gethostbyname(ares, proxyhost.c_str(), PF_INET, proxy_ready_callback, httpctx);
}

bool CurlHttpIO::crackurl(string *url, string *hostname, int *port)
{
    *port = 0;
    hostname->clear();

    if(!url || !url->size() || !hostname || !port)
        return false;

    size_t starthost, endhost, startport, endport;

    starthost = url->find("://");
    if(starthost != string::npos)
        starthost += 3;
    else
        starthost = 0;

    if((*url)[starthost] == '[' && url->size() > 0)
        starthost++;

    startport = url->find("]:", starthost);
    if(startport == string::npos)
    {
        startport = url->find(":", starthost);
        if(startport != string::npos)
        {
            endhost = startport;
        }
    }
    else
    {
        endhost = startport;
        startport++;
    }

    if(startport != string::npos)
    {
        startport++;

        endport = url->find("/", startport);
        if(endport == string::npos)
            endport = url->size();

        if(endport <= startport || (endport - startport) > 5)
            *port = -1;
        else
        {
            for(unsigned int i = startport; i < endport; i++)
            {
                int c = url->data()[i];
                if(c < '0' || c > '9')
                {
                    *port = -1;
                     break;
                }
            }
        }

        if(!*port)
        {
            *port = atoi(url->data() + startport);
            if(*port > 65535)
                *port = -1;
        }
    }
    else
    {
        endhost = url->find("]/", starthost);
        if(endhost == string::npos)
        {
            endhost = url->find("/", starthost);
            if(endhost == string::npos)
                endhost = url->size();
        }
    }

    if(!*port)
    {
        if(!url->compare(0, 8, "https://"))
            *port = 443;
        else if(!url->compare(0, 7, "http://"))
            *port = 80;
        else
            *port = -1;
    }

    *hostname = url->substr(starthost, endhost - starthost);
    if(*port <= 0 || starthost == string::npos || starthost >= endhost)
        return false;

    return true;
}

int CurlHttpIO::debug_callback(CURL *, curl_infotype type, char *data, size_t size, void *)
{
    if(type == CURLINFO_TEXT)
    {
        cout << "cURL DEBUG: ";
        cout.width(size);
        cout << data;
    }
    return 0;
}

// POST request to URL
void CurlHttpIO::post(HttpReq* req, const char* data, unsigned len)
{
    CurlHttpContext *httpctx = new CurlHttpContext;
    httpctx->curl = NULL;
    httpctx->httpio = this;
    httpctx->req = req;
    httpctx->len = len;
    httpctx->data = data;
    httpctx->headers = NULL;
    httpctx->ares_pending = 0;

    req->httpiohandle = (void *)httpctx;

    if((proxyurl.size() && !proxyhost.size()) || //Malformed proxy string
            !crackurl(&req->posturl, &httpctx->hostname, &httpctx->port)) //Invalid request
    {
        req->status = REQ_FAILURE;
        return;
    }

    if(!ipv6requestsenabled && ipv6available())
    {
        time_t currenttime;
        time(&currenttime);
        if((currenttime - ipv6deactivationtime) > IPV6_RETRY_INTERVAL_SECS)
        {
            ipv6requestsenabled = true;
        }
    }

    req->in.clear();
    req->status = REQ_INFLIGHT;
    httpctx->ares_pending = 1;

    if(ipv6requestsenabled)
    {
        httpctx->ares_pending++;
        ares_gethostbyname(ares, httpctx->hostname.c_str(), PF_INET6, ares_completed_callback, httpctx);
    }
    ares_gethostbyname(ares, httpctx->hostname.c_str(), PF_INET, ares_completed_callback, httpctx);
}

void CurlHttpIO::setproxy(Proxy* proxy)
{
    //clear the previous proxy IP
    proxyip.clear();

    if(proxy->getProxyType() != Proxy::CUSTOM || !proxy->getProxyURL().size())
    {
        //automatic proxy is not supported
        //invalidate inflight proxy changes
        proxyhost.clear();

        //don't use a proxy
        proxyurl.clear();

        //send pending requests without a proxy
        send_pending_requests();
        return;
    }

    proxyurl = proxy->getProxyURL();
    proxyusername = proxy->getUsername();
    proxypassword = proxy->getPassword();

    if(!crackurl(&proxyurl, &proxyhost, &proxyport))
    {
        //malformed proxy string
        //invalidate inflight proxy changes

        //mark the proxy as invalid (proxyurl set but proxyhost not set)
        proxyhost.clear();

        //drop all pending requests
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
        CurlHttpContext *httpctx = (CurlHttpContext *)req->httpiohandle;

        if(httpctx->curl)
        {
            curl_multi_remove_handle(curlm, httpctx->curl);
            curl_easy_cleanup(httpctx->curl);
            curl_slist_free_all(httpctx->headers);
        }

        httpctx->req = NULL;
        if(req->status == REQ_FAILURE || httpctx->curl)
            delete httpctx;

        req->httpstatus = 0;
        req->status = REQ_FAILURE;

        req->httpiohandle = NULL;
    }
}

// real-time progress information on POST data
m_off_t CurlHttpIO::postpos(void* handle)
{
    double bytes = 0;
    CurlHttpContext* httpctx = (CurlHttpContext *)handle;
    if(httpctx->curl)
        curl_easy_getinfo(httpctx->curl, CURLINFO_SIZE_UPLOAD, &bytes);

    return (m_off_t)bytes;
}

// process events
bool CurlHttpIO::doio()
{
    bool done = false;

    CURLMsg *msg;
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

                if (debug)
                {
                    cout << "CURLMSG_DONE with HTTP status: " << req->httpstatus << endl;

                    if (req->httpstatus)
                    {
                        if (req->binary)
                        {
                            cout << "[received " << req->in.size() << " bytes of raw data]" << endl;
                        }
                        else
                        {
                            cout << "Received: " << req->in.c_str() << endl;
                        }
                    }
                }

                // check httpstatus and response length
                req->status = (req->httpstatus == 200
                            && (req->contentlength < 0
                             || req->contentlength == (req->buf ? req->bufpos : (int)req->in.size())))
                             ? REQ_SUCCESS : REQ_FAILURE;

                inetstatus(req->status);
                
                if (req->status == REQ_SUCCESS)
                {
                    lastdata = Waiter::ds;
                }

                success = true;
                done = true;
            }
            else
            {
                req->status = REQ_FAILURE;
            }

            if(req->status == REQ_FAILURE && !req->httpstatus)
            {
                CurlHttpContext *ctx = (CurlHttpContext *)req->httpiohandle;
                ipv6requestsenabled = !ctx->isIPv6 && ipv6available();
                if(ipv6requestsenabled)
                {
                    //Change the protocol of the proxy after fails contacting
                    //MEGA servers with both protocols (IPv4 and IPv6)
                    ipv6proxyenabled = !ipv6proxyenabled && ipv6available();
                    request_proxy_ip();
                }
                else if(ipv6available())
                {
                    time(&ipv6deactivationtime);
                }
            }
        }

        curl_multi_remove_handle(curlm, msg->easy_handle);
        curl_easy_cleanup(msg->easy_handle);

        if(req)
        {
            CurlHttpContext *httpctx = (CurlHttpContext *)req->httpiohandle;
            curl_slist_free_all(httpctx->headers);
            delete httpctx;
            req->httpiohandle = NULL;
        }
    }

    return done;
}

// callback for incoming HTTP payload
void CurlHttpIO::send_pending_requests()
{
    while(pendingrequests.size())
    {
        CurlHttpContext *httpctx = pendingrequests.front();
        if(httpctx->req)
            send_request(httpctx);
        else
            delete httpctx;

        pendingrequests.pop();
    }
}

void CurlHttpIO::drop_pending_requests()
{
    while(pendingrequests.size())
    {
        CurlHttpContext *httpctx = pendingrequests.front();
        if(httpctx->req)
            httpctx->req->status = REQ_FAILURE;
        else
            delete httpctx;

        pendingrequests.pop();
    }
}

size_t CurlHttpIO::write_data(void* ptr, size_t, size_t nmemb, void* target)
{
    ((HttpReq*)target)->put(ptr, nmemb);
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

} // namespace
