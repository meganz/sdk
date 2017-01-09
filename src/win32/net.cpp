/**
 * @file win32/net.cpp
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

// FIXME: Work around WinHTTP's inability to POST additional data
// after having read from the HTTP connection

#include "meganet.h"

namespace mega {

extern PGTC pGTC;

WinHttpIO::WinHttpIO()
{
    InitializeCriticalSection(&csHTTP);
    EnterCriticalSection(&csHTTP);

    hWakeupEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    waiter = NULL;    
}

WinHttpIO::~WinHttpIO()
{
    WinHttpCloseHandle(hSession);
    LeaveCriticalSection(&csHTTP);
}

void WinHttpIO::setuseragent(string* useragent)
{
    string wuseragent;

    wuseragent.resize((useragent->size() + 1) * sizeof(wchar_t));
    wuseragent.resize(sizeof(wchar_t)
                      * (MultiByteToWideChar(CP_UTF8, 0, useragent->c_str(),
                                              -1, (wchar_t*)wuseragent.data(),
                                              wuseragent.size() / sizeof(wchar_t) + 1)
                          - 1));

    // create the session handle using the default settings.
    hSession = WinHttpOpen((LPCWSTR)wuseragent.data(),
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS,
                           WINHTTP_FLAG_ASYNC);

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 |
            WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
            WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;

    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof (protocols));
}


void WinHttpIO::setproxy(Proxy* proxy)
{
    Proxy* autoProxy = NULL;

    proxyUsername.clear();
    proxyPassword.clear();

    if (proxy->getProxyType() == Proxy::AUTO)
    {
        autoProxy = getautoproxy();
        proxy = autoProxy;
    }

    if (proxy->getProxyType() == Proxy::NONE)
    {
        WINHTTP_PROXY_INFO proxyInfo;
        proxyInfo.dwAccessType = WINHTTP_ACCESS_TYPE_NO_PROXY;
        proxyInfo.lpszProxy = WINHTTP_NO_PROXY_NAME;
        proxyInfo.lpszProxyBypass = WINHTTP_NO_PROXY_BYPASS;
        WinHttpSetOption(hSession, WINHTTP_OPTION_PROXY, &proxyInfo, sizeof(proxyInfo));
        LOG_info << "Proxy disabled";
    }
    else if (proxy->getProxyType() == Proxy::CUSTOM)
    {
        string proxyURL = proxy->getProxyURL();
        proxyURL.append("", 1);
        WINHTTP_PROXY_INFO proxyInfo;
        proxyInfo.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        proxyInfo.lpszProxy = (LPWSTR)proxyURL.data();
        proxyInfo.lpszProxyBypass = WINHTTP_NO_PROXY_BYPASS;
        WinHttpSetOption(hSession, WINHTTP_OPTION_PROXY, &proxyInfo, sizeof(proxyInfo));

        LOG_info << "Proxy enabled";
        if (proxy->credentialsNeeded())
        {
            proxyUsername = proxy->getUsername();

            if (proxyUsername.size())
            {
                proxyUsername.append("", 1);
            }

            proxyPassword = proxy->getPassword();

            if(proxyPassword.size())
            {
                proxyPassword.append("", 1);
            }

            LOG_info << "Proxy requires authentication";
        }
    }

    delete autoProxy;
}

// trigger wakeup
void WinHttpIO::httpevent()
{
    SetEvent(hWakeupEvent);
}

// (WinHTTP unfortunately uses threads, hence the need for a mutex)
void WinHttpIO::lock()
{
    EnterCriticalSection(&csHTTP);
}

void WinHttpIO::unlock()
{
    LeaveCriticalSection(&csHTTP);
}

// ensure wakeup from WinHttpIO events
void WinHttpIO::addevents(Waiter* cwaiter, int flags)
{
    waiter = (WinWaiter*)cwaiter;
    waiter->addhandle(hWakeupEvent, flags);
    waiter->pcsHTTP = &csHTTP;
}

// handle WinHTTP callbacks (which can be in a worker thread context)
VOID CALLBACK WinHttpIO::asynccallback(HINTERNET hInternet, DWORD_PTR dwContext,
                                       DWORD dwInternetStatus,
                                       LPVOID lpvStatusInformation,
                                       DWORD dwStatusInformationLength)
{
    WinHttpContext* httpctx = (WinHttpContext*)dwContext;
    WinHttpIO* httpio = (WinHttpIO*)httpctx->httpio;

    if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
    {
        LOG_verbose << "Closing request";
        assert(!httpctx->req);

        if (httpctx->gzip)
        {
            inflateEnd(&httpctx->z);
        }
        
        delete httpctx;
        return;
    }

    httpio->lock();

    HttpReq* req = httpctx->req;

    // request cancellations that occured after asynccallback() was entered are caught here
    if (!req)
    {
        LOG_verbose << "Request cancelled";
        httpio->unlock();
        return;
    }

    switch (dwInternetStatus)
    {
        case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
        {
            DWORD size = *(DWORD*)lpvStatusInformation;

            if (!size)
            {
                if (req->binary)
                {
                    LOG_debug << "[received " << (req->buf ? req->buflen : req->in.size()) << " bytes of raw data]";
                }
                else
                {
                    if(req->in.size() < 2048)
                    {
                        LOG_debug << "Received: " << req->in.c_str();
                    }
                    else
                    {
                        LOG_debug << "Received: " << req->in.substr(0,2048).c_str();
                    }
                }

                LOG_debug << "Request finished with HTTP status: " << req->httpstatus;
                req->status = (req->httpstatus == 200
                            && (req->contentlength < 0
                             || req->contentlength == (req->buf ? req->bufpos : (int)req->in.size())))
                             ? REQ_SUCCESS : REQ_FAILURE;

                if (req->status == REQ_SUCCESS)
                {
                    httpio->lastdata = Waiter::ds;
                    req->lastdata = Waiter::ds;
                }
                httpio->success = true;
            }
            else
            {
                LOG_verbose << "Data available. Remaining: " << size << " bytes";

                char* ptr;

                if (httpctx->gzip)
                {                    
                    m_off_t zprevsize = httpctx->zin.size();
                    httpctx->zin.resize(zprevsize + size);
                    ptr = (char*)httpctx->zin.data() + zprevsize;
                }
                else
                {                    
                    ptr = (char*)req->reserveput((unsigned*)&size);
                    req->bufpos += size;
                }

                if (!WinHttpReadData(hInternet, ptr, size, NULL))
                {
                    LOG_err << "Unable to get more data. Code: " << GetLastError();
                    httpio->cancel(req);
                }
            }

            httpio->httpevent();
            break;
        }

        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            LOG_verbose << "Read complete";

            if (dwStatusInformationLength)
            {
                LOG_verbose << dwStatusInformationLength << " bytes received";
                if (req->httpio)
                {
                    req->httpio->lastdata = Waiter::ds;
                    req->lastdata = Waiter::ds;
                }
            
                if (httpctx->gzip)
                {                    
                    httpctx->z.next_in = (Bytef*)lpvStatusInformation;
                    httpctx->z.avail_in = dwStatusInformationLength;

                    req->bufpos += httpctx->z.avail_out;
                    int t = inflate(&httpctx->z, Z_SYNC_FLUSH);
                    req->bufpos -= httpctx->z.avail_out;

                    if ((char*)lpvStatusInformation + dwStatusInformationLength ==
                             httpctx->zin.data() + httpctx->zin.size())
                    {
                        httpctx->zin.clear();
                    }

                    if (t != Z_OK && (t != Z_STREAM_END || httpctx->z.avail_out))
                    {
                        LOG_err << "GZIP error";
                        httpio->cancel(req);
                    }
                }

                if (!WinHttpQueryDataAvailable(httpctx->hRequest, NULL))
                {
                    LOG_err << "Error on WinHttpQueryDataAvailable. Code: " << GetLastError();
                    httpio->cancel(req);
                    httpio->httpevent();
                }
            }
            break;

        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
        {
            DWORD statusCode;
            DWORD statusCodeSize = sizeof(statusCode);

            if (!WinHttpQueryHeaders(httpctx->hRequest,
                                     WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     WINHTTP_HEADER_NAME_BY_INDEX,
                                     &statusCode,
                                     &statusCodeSize,
                                     WINHTTP_NO_HEADER_INDEX))
            {
                LOG_err << "Error getting headers. Code: " << GetLastError();
                httpio->cancel(req);
                httpio->httpevent();
            }
            else
            {
                LOG_verbose << "Headers available";

                req->httpstatus = statusCode;

                if (req->httpio)
                {
                    req->httpio->lastdata = Waiter::ds;
                    req->lastdata = Waiter::ds;
                }

                if (!req->buf)
                {
                    DWORD timeLeft;
                    DWORD timeLeftSize = sizeof(timeLeft);
                    if (WinHttpQueryHeaders(httpctx->hRequest,
                                            WINHTTP_QUERY_CUSTOM | WINHTTP_QUERY_FLAG_NUMBER,
                                            L"X-MEGA-Time-Left",
                                            &timeLeft,
                                            &timeLeftSize,
                                            WINHTTP_NO_HEADER_INDEX))
                    {
                        LOG_verbose << "Seconds left until more bandwidth quota: " << timeLeft;
                        req->timeleft = timeLeft;
                    }

                    // obtain original content length - always present if gzip is in use
                    DWORD contentLength;
                    DWORD contentLengthSize = sizeof(contentLength);

                    if (WinHttpQueryHeaders(httpctx->hRequest,
                                            WINHTTP_QUERY_CUSTOM | WINHTTP_QUERY_FLAG_NUMBER,
                                            L"Original-Content-Length",
                                            &contentLength,
                                            &contentLengthSize,
                                            WINHTTP_NO_HEADER_INDEX))
                    {
                        LOG_verbose << "Content-Length: " << contentLength;
                        req->setcontentlength(contentLength);

                        // check for gzip content encoding
                        WCHAR contentEncoding[16];
                        DWORD contentEncodingSize = sizeof(contentEncoding);

                        httpctx->gzip = WinHttpQueryHeaders(httpctx->hRequest,
                                                WINHTTP_QUERY_CONTENT_ENCODING,
                                                WINHTTP_HEADER_NAME_BY_INDEX,
                                                &contentEncoding,
                                                &contentEncodingSize,
                                                WINHTTP_NO_HEADER_INDEX)
                                    && !wcscmp(contentEncoding, L"gzip");

                        if (httpctx->gzip)
                        {
                            LOG_verbose << "Using GZIP";

                            httpctx->z.zalloc = Z_NULL;
                            httpctx->z.zfree = Z_NULL;
                            httpctx->z.opaque = Z_NULL;
                            httpctx->z.avail_in = 0;
                            httpctx->z.next_in = Z_NULL;

                            inflateInit2(&httpctx->z, MAX_WBITS+16);

                            req->in.resize(contentLength);
                            httpctx->z.avail_out = contentLength;
                            httpctx->z.next_out = (unsigned char*)req->in.data();
                        }
                        else
                        {
                            LOG_verbose << "Not using GZIP";
                        }
                    }
                    else
                    {
                        LOG_verbose << "Content-Length not available";
                    }
                }

                if (!WinHttpQueryDataAvailable(httpctx->hRequest, NULL))
                {
                    LOG_err << "Unable to query data. Code: " << GetLastError();

                    httpio->cancel(req);
                    httpio->httpevent();
                }
                else if (httpio->waiter && httpio->noinetds)
                {
                    httpio->inetstatus(true);
                }
            }

            break;
        }

        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        {
            DWORD e = GetLastError();

            LOG_err << "Request error. Code: " << e;

            if (httpio->waiter && e != ERROR_WINHTTP_TIMEOUT)
            {
                httpio->inetstatus(false);
            }
        }
        // fall through
        case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
            if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE)
            {
                LOG_err << "Security check failed. Code: " << (*(DWORD*)lpvStatusInformation);
            }

            httpio->cancel(req);
            httpio->httpevent();
            break;

        case WINHTTP_CALLBACK_STATUS_SENDING_REQUEST:
        {
            if (MegaClient::disablepkp || !req->protect)
            {
                break;
            }

            PCCERT_CONTEXT cert;
            DWORD len = sizeof cert;

            if (WinHttpQueryOption(httpctx->hRequest, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &cert, &len))
            {
                CRYPT_BIT_BLOB* pkey = &cert->pCertInfo->SubjectPublicKeyInfo.PublicKey;

                // this is an SSL connection: verify public key to prevent MITM attacks
                if (pkey->cbData != 270
                 || (memcmp(pkey->pbData, "\x30\x82\x01\x0a\x02\x82\x01\x01\x00" APISSLMODULUS1
                                          "\x02" APISSLEXPONENTSIZE APISSLEXPONENT, 270)
                  && memcmp(pkey->pbData, "\x30\x82\x01\x0a\x02\x82\x01\x01\x00" APISSLMODULUS2
                                          "\x02" APISSLEXPONENTSIZE APISSLEXPONENT, 270)))
                {
                    LOG_err << "Invalid public key. Possible MITM attack!!";
                    req->sslcheckfailed = true;

                    CertFreeCertificateContext(cert);
                    httpio->cancel(req);
                    httpio->httpevent();
                    break;
                }

                CertFreeCertificateContext(cert);
            }

            break;
        }

        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
        case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
            if (httpctx->postpos < httpctx->postlen)
            {
                LOG_verbose << "Chunk written";
                unsigned pos = httpctx->postpos;
                unsigned t = httpctx->postlen - pos;

                if (t > HTTP_POST_CHUNK_SIZE)
                {
                    t = HTTP_POST_CHUNK_SIZE;
                }

                httpctx->postpos += t;

                if (!WinHttpWriteData(httpctx->hRequest, (LPVOID)(httpctx->postdata + pos), t, NULL))
                {
                    LOG_err << "Error writting data. Code: " << GetLastError();
                    req->httpio->cancel(req);
                }

                httpio->httpevent();
            }
            else
            {
                LOG_verbose << "Request written";
                if (!WinHttpReceiveResponse(httpctx->hRequest, NULL))
                {
                    LOG_err << "Error receiving response. Code: " << GetLastError();
                    httpio->cancel(req);
                    httpio->httpevent();
                }

                httpctx->postdata = NULL;
            }
    }

    httpio->unlock();
}

// POST request to URL
void WinHttpIO::post(HttpReq* req, const char* data, unsigned len)
{
    LOG_debug << "POST target URL: " << req->posturl;

    if (req->binary)
    {
        LOG_debug << "[sending " << (data ? len : req->out->size()) << " bytes of raw data]";
    }
    else
    {
        LOG_debug << "Sending: " << *req->out;
    }

    WinHttpContext* httpctx;

    WCHAR szURL[8192];
    WCHAR szHost[256];
    URL_COMPONENTS urlComp = { sizeof urlComp };

    urlComp.lpszHostName = szHost;
    urlComp.dwHostNameLength = sizeof szHost / sizeof *szHost;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwSchemeLength = (DWORD)-1;

    httpctx = new WinHttpContext;

    httpctx->httpio = this;
    httpctx->req = req;
    httpctx->gzip = false;

    req->httpiohandle = (void*)httpctx;

    if (MultiByteToWideChar(CP_UTF8, 0, req->posturl.c_str(), -1, szURL,
                            sizeof szURL / sizeof *szURL)
     && WinHttpCrackUrl(szURL, 0, 0, &urlComp))
    {
        if ((httpctx->hConnect = WinHttpConnect(hSession, szHost, urlComp.nPort, 0)))
        {
            httpctx->hRequest = WinHttpOpenRequest(httpctx->hConnect, L"POST",
                                                   urlComp.lpszUrlPath, NULL,
                                                   WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                   (urlComp.nScheme == INTERNET_SCHEME_HTTPS)
                                                   ? WINHTTP_FLAG_SECURE
                                                   : 0);

            if (httpctx->hRequest)
            {
                if (proxyUsername.size())
                {
                    LOG_verbose << "Setting proxy credentials";

                    WinHttpSetCredentials(httpctx->hRequest, WINHTTP_AUTH_TARGET_PROXY,
                                          WINHTTP_AUTH_SCHEME_BASIC,
                                          (LPWSTR)proxyUsername.data(), (LPWSTR)proxyPassword.data(), NULL);
                }

                WinHttpSetTimeouts(httpctx->hRequest, 58000, 58000, 0, 0);

                WinHttpSetStatusCallback(httpctx->hRequest, asynccallback,
                                         WINHTTP_CALLBACK_FLAG_DATA_AVAILABLE
                                       | WINHTTP_CALLBACK_FLAG_READ_COMPLETE
                                       | WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE
                                       | WINHTTP_CALLBACK_FLAG_REQUEST_ERROR
                                       | WINHTTP_CALLBACK_FLAG_SECURE_FAILURE
                                       | WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE
                                       | WINHTTP_CALLBACK_FLAG_SEND_REQUEST
                                       | WINHTTP_CALLBACK_FLAG_WRITE_COMPLETE
                                       | WINHTTP_CALLBACK_FLAG_HANDLES,
                                         0);

                LPCWSTR pwszHeaders = req->type == REQ_JSON || !req->buf
                                    ? L"Content-Type: application/json\r\nAccept-Encoding: gzip"
                                    : L"Content-Type: application/octet-stream";

                httpctx->postlen = data ? len : req->out->size();
                httpctx->postdata = data ? data : req->out->data();

                if (urlComp.nPort == 80)
                {
                    LOG_verbose << "HTTP connection";

                    // HTTP connection: send a chunk of data immediately
                    httpctx->postpos = (httpctx->postlen < HTTP_POST_CHUNK_SIZE)
                                      ? httpctx->postlen
                                      : HTTP_POST_CHUNK_SIZE;
                }
                else
                {
                    LOG_verbose << "HTTPS connection";

                    // HTTPS connection: ignore certificate errors, send no data yet
                    DWORD flags = SECURITY_FLAG_IGNORE_CERT_CN_INVALID
                                | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
                                | SECURITY_FLAG_IGNORE_UNKNOWN_CA;

                    WinHttpSetOption(httpctx->hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof flags);

                    httpctx->postpos = 0;
                }

                if (WinHttpSendRequest(httpctx->hRequest, pwszHeaders,
                                       wcslen(pwszHeaders),
                                       (LPVOID)httpctx->postdata,
                                       httpctx->postpos,
                                       httpctx->postlen,
                                       (DWORD_PTR)httpctx))
                {
                    LOG_verbose << "Request sent";
                    req->status = REQ_INFLIGHT;
                    return;
                }

                LOG_err << "Error sending request: " << req->posturl << "  Code: " << GetLastError();
            }
            else
            {
                LOG_err << "Error opening request: " << req->posturl << "  Code: " << GetLastError();
            }
        }
        else
        {
            LOG_err << "Error connecting to " << req->posturl << "  Code: " << GetLastError();
            httpctx->hRequest = NULL;
        }
    }
    else
    {
        LOG_err << "Error parsing POST URL: " << req->posturl << "  Code: " << GetLastError();
        httpctx->hRequest = NULL;
        httpctx->hConnect = NULL;
    }

    LOG_err << "Request failed";
    req->status = REQ_FAILURE;
}

// cancel pending HTTP request
void WinHttpIO::cancel(HttpReq* req)
{
    WinHttpContext* httpctx;

    if ((httpctx = (WinHttpContext*)req->httpiohandle))
    {
        httpctx->req = NULL;

        req->httpstatus = 0;
        req->status = REQ_FAILURE;
        req->httpiohandle = NULL;

        if (httpctx->hConnect)
        {
            WinHttpCloseHandle(httpctx->hConnect);
        }

        if (httpctx->hRequest)
        {
            WinHttpCloseHandle(httpctx->hRequest);
        }
    }
}

// supply progress information on POST data
m_off_t WinHttpIO::postpos(void* handle)
{
    return ((WinHttpContext*)handle)->postpos;
}

// process events
bool WinHttpIO::doio()
{
    return false;
}
} // namespace
