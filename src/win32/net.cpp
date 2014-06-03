/**
 * @file win32/net.cpp
 * @brief Win32 network access layer (using WinHTTP)
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

#include "meganet.h"

namespace mega {
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
}

// trigger wakeup
void WinHttpIO::httpevent()
{
    SetEvent(hWakeupEvent);
}

// (WinHTTP unfortunately uses threads, hence the need for a mutex)
void WinHttpIO::entercs()
{
    EnterCriticalSection(&csHTTP);
}

void WinHttpIO::leavecs()
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
        assert(!httpctx->req);

        if (httpctx->gzip)
        {
            inflateEnd(&httpctx->z);
        }
        
        delete httpctx;
        return;
    }

    httpio->entercs();

    HttpReq* req = httpctx->req;

    // request cancellations that occured after asynccallback() was entered are caught here
    if (!req)
    {
        httpio->leavecs();
        return;
    }

    switch (dwInternetStatus)
    {
        case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
        {
            DWORD size = *(DWORD*)lpvStatusInformation;

            if (!size)
            {
                if (debug)
                {
                    if (req->binary)
                    {
                        cout << "[received " << req->bufpos << " bytes of raw data]" << endl;
                    }
                    else
                    {
                        cout << "Received: " << req->in.c_str() << endl;
                    }
                }

                req->status = req->httpstatus == 200 ? REQ_SUCCESS : REQ_FAILURE;
                httpio->success = true;
            }
            else
            {
                char* ptr;

                if (httpctx->gzip)
                {
                    m_off_t zprevsize = httpctx->zin.size();
                    httpctx->zin.resize(zprevsize + size);
                    ptr = (char*) httpctx->zin.data() + zprevsize;
                }
                else
                {
                    ptr = (char*)req->reserveput((unsigned*)&size);
                    req->bufpos += size;
                }

                if (!WinHttpReadData(hInternet, ptr, size, NULL))
                {
                    httpio->cancel(req);
                }
            }

            httpio->httpevent();
            break;
        }

        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            if (dwStatusInformationLength)
            {
                if (httpctx->gzip)
                {
                    httpctx->z.next_in = (Bytef*)lpvStatusInformation;
                    httpctx->z.avail_in = dwStatusInformationLength;

                    req->bufpos += httpctx->z.avail_out;
                    int t = inflate(&httpctx->z, Z_SYNC_FLUSH);
                    req->bufpos -= httpctx->z.avail_out;

                    if (((char *)lpvStatusInformation + dwStatusInformationLength) ==
                             (httpctx->zin.data() + httpctx->zin.size()))
                    {
                        httpctx->zin.clear();
                    }

                    if (t != Z_OK && (t != Z_STREAM_END || httpctx->z.avail_out))
                    {
                        httpio->cancel(req);
                    }
                }

                if (!WinHttpQueryDataAvailable(httpctx->hRequest, NULL))
                {
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
                httpio->cancel(req);
                httpio->httpevent();
            }
            else
            {
                req->httpstatus = statusCode;

                if (!req->buf)
                {
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
                    }
                }

                if (!WinHttpQueryDataAvailable(httpctx->hRequest, NULL))
                {
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
            if (httpio->waiter && GetLastError() != ERROR_WINHTTP_TIMEOUT)
            {
                httpio->inetstatus(false);
            }
            // fall through
        case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
            httpio->cancel(req);
            httpio->httpevent();
            break;

        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
        {
            PCCERT_CONTEXT cert;
            DWORD len = sizeof cert;

            if (WinHttpQueryOption(httpctx->hRequest, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &cert, &len))
            {
                CRYPT_BIT_BLOB* pkey = &cert->pCertInfo->SubjectPublicKeyInfo.PublicKey;

                // this is an SSL connection: prevent MITM
                if (pkey->cbData != 270 || memcmp(pkey->pbData, "\x30\x82\x01\x0a\x02\x82\x01\x01"
                                                                "\x00\xb6\x61\xe7\xcf\x69\x2a\x84"
                                                                "\x35\x05\xc3\x14\xbc\x95\xcf\x94"
                                                                "\x33\x1c\x82\x67\x3b\x04\x35\x11"
                                                                "\xa0\x8d\xc8\x9d\xbb\x9c\x79\x65"
                                                                "\xe7\x10\xd9\x91\x80\xc7\x81\x0c"
                                                                "\xf4\x95\xbb\xb3\x26\x9b\x97\xd2"
                                                                "\x14\x0f\x0b\xca\xf0\x5e\x45\x7b"
                                                                "\x32\xc6\xa4\x7d\x7a\xfe\x11\xe7"
                                                                "\xb2\x5e\x21\x55\x23\x22\x1a\xca"
                                                                "\x1a\xf9\x21\xe1\x4e\xb7\x82\x0d"
                                                                "\xeb\x9d\xcb\x4e\x3d\x0b\xe4\xed"
                                                                "\x4a\xef\xe4\xab\x0c\xec\x09\x69"
                                                                "\xfe\xae\x43\xec\x19\x04\x3d\x5b"
                                                                "\x68\x0f\x67\xe8\x80\xff\x9b\x03"
                                                                "\xea\x50\xab\x16\xd7\xe0\x4c\xb4"
                                                                "\x42\xef\x31\xe2\x32\x9f\xe4\xd5"
                                                                "\xf4\xd8\xfd\x82\xcc\xc4\x50\xd9"
                                                                "\x4d\xb5\xfb\x6d\xa2\xf3\xaf\x37"
                                                                "\x67\x7f\x96\x4c\x54\x3d\x9b\x1c"
                                                                "\xbd\x5c\x31\x6d\x10\x43\xd8\x22"
                                                                "\x21\x01\x87\x63\x22\x89\x17\xca"
                                                                "\x92\xcb\xcb\xec\xe8\xc7\xff\x58"
                                                                "\xe8\x18\xc4\xce\x1b\xe5\x4f\x20"
                                                                "\xa8\xcf\xd3\xb9\x9d\x5a\x7a\x69"
                                                                "\xf2\xca\x48\xf8\x87\x95\x3a\x32"
                                                                "\x70\xb3\x1a\xf0\xc4\x45\x70\x43"
                                                                "\x58\x18\xda\x85\x29\x1d\xaf\x83"
                                                                "\xc2\x35\xa9\xc1\x73\x76\xb4\x47"
                                                                "\x22\x2b\x42\x9f\x93\x72\x3f\x9d"
                                                                "\x3d\xa1\x47\x3d\xb0\x46\x37\x1b"
                                                                "\xfd\x0e\x28\x68\xa0\xf6\x1d\x62"
                                                                "\xb2\xdc\x69\xc7\x9b\x09\x1e\xb5"
                                                                "\x47\x02\x03\x01\x00\x01",270))
                {
                    httpio->cancel(req);
                    httpio->httpevent();
                    break;
                }
            }
        }
        case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
            if (httpctx->postpos < httpctx->postlen)
            {
                unsigned pos = httpctx->postpos;
                unsigned t = httpctx->postlen - pos;

                if (t > HTTP_POST_CHUNK_SIZE)
                {
                    t = HTTP_POST_CHUNK_SIZE;
                }

                httpctx->postpos += t;

                if (!WinHttpWriteData(httpctx->hRequest, (LPVOID)(httpctx->postdata + pos), t, NULL))
                {
                    req->httpio->cancel(req);
                }

                httpio->httpevent();
            }
            else
            {
                if (!WinHttpReceiveResponse(httpctx->hRequest, NULL))
                {
                    httpio->cancel(req);
                    httpio->httpevent();
                }
            }
    }

    httpio->leavecs();
}

// POST request to URL
void WinHttpIO::post(HttpReq* req, const char* data, unsigned len)
{
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
                WinHttpSetTimeouts(httpctx->hRequest, 0, 20000, 20000, 1800000);

                WinHttpSetStatusCallback(httpctx->hRequest, asynccallback,
                                         WINHTTP_CALLBACK_FLAG_DATA_AVAILABLE
                                         | WINHTTP_CALLBACK_FLAG_READ_COMPLETE
                                         | WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE
                                         | WINHTTP_CALLBACK_FLAG_REQUEST_ERROR
                                         | WINHTTP_CALLBACK_FLAG_SECURE_FAILURE
                                         | WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE
                                         | WINHTTP_CALLBACK_FLAG_WRITE_COMPLETE
                                         | WINHTTP_CALLBACK_FLAG_HANDLES,
                                         0);

                LPCWSTR pwszHeaders = (req->type == REQ_JSON || !req->buf)
                                      ? L"Content-Type: application/json\r\nAccept-Encoding: gzip"
                                      : L"Content-Type: application/octet-stream";

                // data is sent in HTTP_POST_CHUNK_SIZE instalments to ensure
                // semi-smooth UI progress info
                httpctx->postlen = data ? len : req->out->size();
                httpctx->postdata = data ? data : req->out->data();
                httpctx->postpos = (urlComp.nPort == 80)
                                   ? ((httpctx->postlen < HTTP_POST_CHUNK_SIZE)
                                      ? httpctx->postlen
                                      : HTTP_POST_CHUNK_SIZE)
                                   : 0;

                if (WinHttpSendRequest(httpctx->hRequest, pwszHeaders,
                                       wcslen(pwszHeaders),
                                       (LPVOID)httpctx->postdata,
                                       httpctx->postpos,
                                       httpctx->postlen,
                                       (DWORD_PTR)httpctx))
                {
                    req->status = REQ_INFLIGHT;
                    return;
                }
            }
        }
        else
        {
            httpctx->hRequest = NULL;
        }
    }
    else
    {
        httpctx->hRequest = NULL;
        httpctx->hConnect = NULL;
    }

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
