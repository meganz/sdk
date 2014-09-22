/**
 * @file posix/net.cpp
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

#include "mega.h"

#include <netdb.h>

namespace mega {
CurlHttpIO::CurlHttpIO()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    ares_library_init(ARES_LIB_INIT_ALL);

    curlm = curl_multi_init();
    ares_init(&ares);

    curlsh = curl_share_init();
    curl_share_setopt(curlsh, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(curlsh, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);

    contenttypejson = curl_slist_append(NULL, "Content-Type: application/json");
    contenttypejson = curl_slist_append(contenttypejson, "Expect:");

    contenttypebinary = curl_slist_append(NULL, "Content-Type: application/octet-stream");
    contenttypebinary = curl_slist_append(contenttypebinary, "Expect:");
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

void CurlHttpIO::setdnsservers(const char *servers)
{
	if (servers)
		ares_set_servers_csv(ares, servers);
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

void CurlHttpIO::ares_completed_callback(void *arg, int status, int, struct hostent *host)
{
    CurlHttpContext* httpctx = (CurlHttpContext *)arg;
    HttpReq* req = httpctx->req;
    if(!req) //The request was cancelled
    {
        delete httpctx;
        return;
    }

    if(status != ARES_SUCCESS || !host || !host->h_addr_list[0])
    {
        req->status = REQ_FAILURE;
        return;
    }

    char ip[INET6_ADDRSTRLEN];
    int len = httpctx->len;
    const char* data = httpctx->data;

    string port;
    unsigned int num = httpctx->port;
    int digits = 0;
    while(num)
    {
        num /= 10;
        digits++;
    }

    port.resize(digits + 2);
    sprintf((char *)port.data(), ":%d:", httpctx->port);

    CurlHttpIO *httpio = httpctx->httpio;

    inet_ntop(host->h_addrtype, host->h_addr_list[0], ip, sizeof(ip));
    string dnsrecord = httpctx->hostname + port + ip;
    httpctx->resolve = curl_slist_append(NULL, dnsrecord.c_str());

    if (debug)
    {
        cout << "POST target URL: " << req->posturl << endl;

        if (req->binary)
        {
            cout << "[sending " << req->out->size() << " bytes of raw data]" << endl;
        }
        else
        {
            cout << "Sending: " << *req->out << endl;
        }
    }

    CURL* curl;

    if ((curl = curl_easy_init()))
    {
        curl_easy_setopt(curl, CURLOPT_RESOLVE, httpctx->resolve);
        curl_easy_setopt(curl, CURLOPT_URL, req->posturl.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data ? data : req->out->data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data ? len : req->out->size());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, httpio->useragent->c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, req->type == REQ_JSON ? httpio->contenttypejson : httpio->contenttypebinary);
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

#ifdef __ANDROID__
        //cURL can't find the certstore on Android,
        //so we rely on the public key pinning
        curl_easy_setopt(curl, CURLOPT_CAINFO, NULL);
        curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);
#endif

        if(httpio->proxyurl.size())
        {
            curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);

            curl_easy_setopt(curl, CURLOPT_PROXY, httpio->proxyurl.c_str());
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

bool CurlHttpIO::crackurl(string *url, string *hostname, int *port)
{
    *port = 0;
    size_t starthost, endhost, startport, endport;

    starthost = url->find_first_of("://");
    if(starthost != string::npos)
    {
        starthost += 3;
        startport = url->find_first_of(":", starthost);
        if(startport != string::npos)
        {
            endhost = startport;
            startport++;

            endport = url->find_first_of("/", startport);
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
            endhost = url->find_first_of("/", starthost);
            if(endhost == string::npos)
                endhost = url->size();
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
    }

    if(*port <= 0 || starthost == string::npos || starthost >= endhost)
        return false;

    *hostname = url->substr(starthost, endhost - starthost);
    return true;
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
    httpctx->resolve = NULL;

    req->httpiohandle = (void *)httpctx;

    if(!crackurl(&req->posturl, &httpctx->hostname, &httpctx->port))
    {
        req->status = REQ_FAILURE;
        return;
    }

    req->in.clear();
    req->status = REQ_INFLIGHT;
    ares_gethostbyname(ares, httpctx->hostname.c_str(), PF_INET, ares_completed_callback, httpctx);
}

void CurlHttpIO::setproxy(Proxy* proxy)
{
    if(proxy->getProxyType() != Proxy::CUSTOM)
    {
        //Automatic proxy is not supported
        proxyurl.clear();
        return;
    }

    proxyurl = proxy->getProxyURL();
    proxyusername = proxy->getUsername();
    proxypassword = proxy->getPassword();
}

Proxy* CurlHttpIO::getautoproxy()
{
    Proxy *proxy = new Proxy();
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
            curl_slist_free_all(httpctx->resolve);
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
        HttpReq* req;

        if ((curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (char**)&req) == CURLE_OK) && req)
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
                            && req->contentlength == (req->buf ? req->bufpos : (int)req->in.size()))
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
        }

        curl_multi_remove_handle(curlm, msg->easy_handle);
        curl_easy_cleanup(msg->easy_handle);

        CurlHttpContext *httpctx = (CurlHttpContext *)req->httpiohandle;
        curl_slist_free_all(httpctx->resolve);
        delete httpctx;
    }

    return done;
}

// callback for incoming HTTP payload
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
