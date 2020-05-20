/**
 * @file http.cpp
 * @brief Generic host HTTP I/O interface
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

#include "mega/http.h"
#include "mega/megaclient.h"
#include "mega/logging.h"
#include "mega/proxy.h"
#include "mega/base64.h"
#include "mega/testhooks.h"

#if defined(WIN32) && !defined(WINDOWS_PHONE)
#include <winhttp.h>
#endif

#if defined(__APPLE__) && !(TARGET_OS_IPHONE)
#include "mega/osx/osxutils.h"
#endif

namespace mega {

// interval to calculate the mean speed (ds)
const int SpeedController::SPEED_MEAN_INTERVAL_DS = 50;

// max time to calculate the mean speed
const int SpeedController::SPEED_MAX_VALUES = 10000;

// data receive timeout (ds)
const int HttpIO::NETWORKTIMEOUT = 6000;

// request timeout (ds)
const int HttpIO::REQUESTTIMEOUT = 1200;

// wait request timeout (ds)
const int HttpIO::SCREQUESTTIMEOUT = 400;

// connect timeout (ds)
const int HttpIO::CONNECTTIMEOUT = 120;

#ifdef _WIN32
const char* mega_inet_ntop(int af, const void* src, char* dst, int cnt)
{
    wchar_t ip[INET6_ADDRSTRLEN];
    int len = INET6_ADDRSTRLEN;
    int ret = 1;

    if (af == AF_INET)
    {
        struct sockaddr_in in = {};
        in.sin_family = AF_INET;
        memcpy(&in.sin_addr, src, sizeof(struct in_addr));
        ret = WSAAddressToString((struct sockaddr*) &in, sizeof(struct sockaddr_in), 0, ip, (LPDWORD)&len);
    }
    else if (af == AF_INET6)
    {
        struct sockaddr_in6 in = {};
        in.sin6_family = AF_INET6;
        memcpy(&in.sin6_addr, src, sizeof(struct in_addr6));
        ret = WSAAddressToString((struct sockaddr*) &in, sizeof(struct sockaddr_in6), 0, ip, (LPDWORD)&len);
    }

    if (ret != 0)
    {
        return NULL;
    }

    if (!WideCharToMultiByte(CP_UTF8, 0, ip, len, dst, cnt, NULL, NULL))
    {
        return NULL;
    }

    return dst;
}
#endif

HttpIO::HttpIO()
{
    success = false;
    noinetds = 0;
    inetback = false;
    lastdata = NEVER;
    downloadSpeed = 0;
    uploadSpeed = 0;
}

// signal Internet status - if the Internet was down for more than one minute,
// set the inetback flag to trigger a reconnect
void HttpIO::inetstatus(bool up)
{
    if (up)
    {
        if (noinetds && Waiter::ds - noinetds > 600)
        {
            inetback = true;
        }

        noinetds = 0;
    }
    else if (!noinetds)
    {
        noinetds = Waiter::ds;
    }
}

// returns true once if an outage just ended
bool HttpIO::inetisback()
{
    if(inetback)
    {
        inetback = false;
        return true;
    }

    return false;
}

void HttpIO::updatedownloadspeed(m_off_t size)
{
    downloadSpeed = downloadSpeedController.calculateSpeed(size);
}

void HttpIO::updateuploadspeed(m_off_t size)
{
    uploadSpeed = uploadSpeedController.calculateSpeed(size);
}

Proxy *HttpIO::getautoproxy()
{
    Proxy* proxy = new Proxy();
    proxy->setProxyType(Proxy::NONE);

#if defined(WIN32) && !defined(WINDOWS_PHONE)
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ieProxyConfig = { 0 };

    if (WinHttpGetIEProxyConfigForCurrentUser(&ieProxyConfig) == TRUE)
    {
        if (ieProxyConfig.lpszProxy)
        {
            string proxyURL;
            proxy->setProxyType(Proxy::CUSTOM);
            int len = static_cast<int>(wcslen(ieProxyConfig.lpszProxy));
            proxyURL.assign((const char*)ieProxyConfig.lpszProxy, len * sizeof(wchar_t) + 1);

            // only save one proxy
            for (int i = 0; i < len; i++)
            {
                wchar_t* character = (wchar_t*)(proxyURL.data() + i * sizeof(wchar_t));

                if (*character == ' ' || *character == ';')
                {
                    proxyURL.resize(i*sizeof(wchar_t));
                    len = i;
                    break;
                }
            }

            // remove protocol prefix, if any
            for (int i = len - 1; i >= 0; i--)
            {
                wchar_t* character = (wchar_t*)(proxyURL.data() + i * sizeof(wchar_t));

                if (*character == '/' || *character == '=')
                {
                    proxyURL = proxyURL.substr((i + 1) * sizeof(wchar_t));
                    break;
                }
            }

            proxy->setProxyURL(&proxyURL);
        }
        else if (ieProxyConfig.lpszAutoConfigUrl || ieProxyConfig.fAutoDetect == TRUE)
        {
            WINHTTP_AUTOPROXY_OPTIONS autoProxyOptions;

            if (ieProxyConfig.lpszAutoConfigUrl)
            {
                autoProxyOptions.dwFlags = WINHTTP_AUTOPROXY_CONFIG_URL;
                autoProxyOptions.lpszAutoConfigUrl = ieProxyConfig.lpszAutoConfigUrl;
                autoProxyOptions.dwAutoDetectFlags = 0;
            }
            else
            {
                autoProxyOptions.dwFlags = WINHTTP_AUTOPROXY_AUTO_DETECT;
                autoProxyOptions.lpszAutoConfigUrl = NULL;
                autoProxyOptions.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
            }

            autoProxyOptions.fAutoLogonIfChallenged = TRUE;
            autoProxyOptions.lpvReserved = NULL;
            autoProxyOptions.dwReserved = 0;

            WINHTTP_PROXY_INFO proxyInfo;

            HINTERNET hSession = WinHttpOpen(L"MEGAsync proxy detection",
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS,
                                   WINHTTP_FLAG_ASYNC);

            if (WinHttpGetProxyForUrl(hSession, L"https://g.api.mega.co.nz/", &autoProxyOptions, &proxyInfo))
            {
                if (proxyInfo.lpszProxy)
                {
                    string proxyURL;
                    proxy->setProxyType(Proxy::CUSTOM);
                    proxyURL.assign((const char*)proxyInfo.lpszProxy, wcslen(proxyInfo.lpszProxy) * sizeof(wchar_t));
                    proxy->setProxyURL(&proxyURL);
                }
            }
            WinHttpCloseHandle(hSession);
        }
    }

    if (ieProxyConfig.lpszProxy)
    {
        GlobalFree(ieProxyConfig.lpszProxy);
    }

    if (ieProxyConfig.lpszProxyBypass)
    {
        GlobalFree(ieProxyConfig.lpszProxyBypass);
    }

    if (ieProxyConfig.lpszAutoConfigUrl)
    {
        GlobalFree(ieProxyConfig.lpszAutoConfigUrl);
    }    
#endif

#if defined(__APPLE__) && !(TARGET_OS_IPHONE)
    getOSXproxy(proxy);
#endif

    return proxy;
}

void HttpIO::getMEGADNSservers(string *dnsservers, bool getfromnetwork)
{
    if (!dnsservers)
    {
        return;
    }

    dnsservers->clear();
    if (getfromnetwork)
    {
        struct addrinfo *aiList = NULL;
        struct addrinfo *hp;

        struct addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;

#ifndef __MINGW32__
        hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;
#endif

        if (!getaddrinfo("ns.mega.co.nz", NULL, &hints, &aiList))
        {
            hp = aiList;
            while (hp)
            {
                char straddr[INET6_ADDRSTRLEN];
                straddr[0] = 0;

                if (hp->ai_family == AF_INET)
                {
                    sockaddr_in *addr = (sockaddr_in *)hp->ai_addr;
                    mega_inet_ntop(hp->ai_family, &addr->sin_addr, straddr, sizeof(straddr));
                }
                else if(hp->ai_family == AF_INET6)
                {
                    sockaddr_in6 *addr = (sockaddr_in6 *)hp->ai_addr;
                    mega_inet_ntop(hp->ai_family, &addr->sin6_addr, straddr, sizeof(straddr));
                }

                if (straddr[0])
                {
                    if(dnsservers->size())
                    {
                        dnsservers->append(",");
                    }
                    dnsservers->append(straddr);
                }

                hp = hp->ai_next;
            }
            freeaddrinfo(aiList);
        }
    }

    if (!getfromnetwork || !dnsservers->size())
    {
        *dnsservers = MEGA_DNS_SERVERS;
        LOG_info << "Using hardcoded MEGA DNS servers: " << *dnsservers;
    }
    else
    {
        LOG_info << "Using current MEGA DNS servers: " << *dnsservers;
    }
}

bool HttpIO::setmaxdownloadspeed(m_off_t)
{
    return false;
}

bool HttpIO::setmaxuploadspeed(m_off_t)
{
    return false;
}

m_off_t HttpIO::getmaxdownloadspeed()
{
    return 0;
}

m_off_t HttpIO::getmaxuploadspeed()
{
    return 0;
}

void HttpReq::post(MegaClient* client, const char* data, unsigned len)
{
    if (httpio)
    {
        LOG_warn << "Ensuring that the request is finished before sending it again";
        httpio->cancel(this);
        init();
    }

    httpio = client->httpio;
    bufpos = 0;
    outpos = 0;
    notifiedbufpos = 0;
    inpurge = 0;
    method = METHOD_POST;
    contentlength = -1;
    lastdata = Waiter::ds;

    DEBUG_TEST_HOOK_HTTPREQ_POST(this)

    httpio->post(this, data, len);
}

void HttpReq::get(MegaClient *client)
{
    if (httpio)
    {
        LOG_warn << "Ensuring that the request is finished before sending it again";
        httpio->cancel(this);
        init();
    }

    httpio = client->httpio;
    bufpos = 0;
    outpos = 0;
    notifiedbufpos = 0;
    inpurge = 0;
    method = METHOD_GET;
    contentlength = -1;
    lastdata = Waiter::ds;

    httpio->post(this);
}

void HttpReq::dns(MegaClient *client)
{
    if (httpio)
    {
        LOG_warn << "Ensuring that the request is finished before sending it again";
        httpio->cancel(this);
        init();
    }
    
    httpio = client->httpio;
    bufpos = 0;
    outpos = 0;
    notifiedbufpos = 0;
    inpurge = 0;
    method = METHOD_NONE;
    contentlength = -1;
    lastdata = Waiter::ds;
    
    httpio->post(this);
}

void HttpReq::disconnect()
{
    if (httpio)
    {
        httpio->cancel(this);
        httpio = NULL;
        init();
    }
}

HttpReq::HttpReq(bool b)
{
    binary = b;
    status = REQ_READY;
    buf = NULL;
    httpio = NULL;
    httpiohandle = NULL;
    out = &outbuf;
    method = METHOD_NONE;
    timeoutms = 0;
    type = REQ_JSON;
    buflen = 0;
    protect = false;
    minspeed = false;

    init();
}

HttpReq::~HttpReq()
{
    if (httpio)
    {
        httpio->cancel(this);
    }

    delete[] buf;
}

void HttpReq::init()
{
    httpstatus = 0;
    inpurge = 0;
    sslcheckfailed = false;
    bufpos = 0;
    notifiedbufpos = 0;
    contentlength = 0;
    timeleft = -1;
    lastdata = NEVER;
    outpos = 0;
    in.clear();
    contenttype.clear();
}

void HttpReq::setreq(const char* u, contenttype_t t)
{
    if (u)
    {
        posturl = u;
    }

    type = t;
}

// add data to fixed or variable buffer
void HttpReq::put(void* data, unsigned len, bool purge)
{
    if (buf)
    {
        if (bufpos + len > buflen)
        {
            len = static_cast<unsigned>(buflen - bufpos);
        }

        memcpy(buf + bufpos, data, len);
    }
    else
    {
        if (inpurge && purge)
        {
            in.erase(0, inpurge);
            inpurge = 0;
        }

        in.append((char*)data, len);
    }
    
    bufpos += len;
}


HttpReq::http_buf_t::http_buf_t(byte* b, size_t s, size_t e)
    : start(s), end(e), buf(b)
{
}

HttpReq::http_buf_t::~http_buf_t()
{
    delete[] buf;
}

void HttpReq::http_buf_t::swap(http_buf_t& other)
{
    byte* tb = buf; buf = other.buf; other.buf = tb;
    size_t ts = start; start = other.start; other.start = ts;
    size_t te = end; end = other.end; other.end = te;
}

bool HttpReq::http_buf_t::isNull()
{
    return buf == NULL;
}

byte* HttpReq::http_buf_t::datastart()
{ 
    return buf + start; 
}

size_t HttpReq::http_buf_t::datalen() 
{ 
    return end - start; 
}


// give up ownership of the buffer for client to use.  
struct HttpReq::http_buf_t* HttpReq::release_buf()
{
    HttpReq::http_buf_t* result = new HttpReq::http_buf_t(buf, inpurge, (size_t)bufpos);
    buf = NULL;
    inpurge = 0;
    buflen = 0;
    bufpos = 0;
    outpos = 0;
    notifiedbufpos = 0;
    contentlength = -1;
    in.clear();
    return result;
}


char* HttpReq::data()
{
    return (char*)in.data() + inpurge;
}

size_t HttpReq::size()
{
    return in.size() - inpurge;
}

// set amount of purgeable in data at 0
void HttpReq::purge(size_t numbytes)
{
    inpurge += numbytes;
}

// set total response size
void HttpReq::setcontentlength(m_off_t len)
{
    if (!buf && type != REQ_BINARY)
    {
        in.reserve(static_cast<size_t>(len));
    }

    contentlength = len;
}

// make space for receiving data; adjust len if out of space
byte* HttpReq::reserveput(unsigned* len)
{
    if (buf)
    {
        if (bufpos + *len > buflen)
        {
            *len = static_cast<unsigned>(buflen - bufpos);
        }

        return buf + bufpos;
    }
    else
    {
        if (inpurge)
        {
            // FIXME: optimize erase()/resize() -> single copy/resize()
            in.erase(0, inpurge);
            bufpos -= inpurge;
            inpurge = 0;
        }

        if (bufpos + *len > (int) in.size())
        {
            in.resize(static_cast<size_t>(bufpos + *len));
        }

        *len = static_cast<unsigned>(in.size() - bufpos);

        return (byte*)in.data() + bufpos;
    }
}

// number of bytes transferred in this request
m_off_t HttpReq::transferred(MegaClient*)
{
    if (buf)
    {
        return bufpos;
    }
    else
    {
        return in.size();
    }
}

HttpReqDL::HttpReqDL()
    : dlpos(0)
    , buffer_released(false)
{
}

// prepare file chunk download
void HttpReqDL::prepare(const char* tempurl, SymmCipher* /*key*/,
                        uint64_t /*ctriv*/, m_off_t pos,
                        m_off_t npos)
{
    char urlbuf[512];

    snprintf(urlbuf, sizeof urlbuf, "%s/%" PRIu64 "-%" PRIu64, tempurl, pos, npos ? npos - 1 : 0);
    setreq(urlbuf, REQ_BINARY);

    dlpos = pos;
    size = (unsigned)(npos - pos);
    buffer_released = false;

    if (!buf || buflen != size)
    {
        // (re)allocate buffer
        if (buf)
        {
            delete[] buf;
            buf = NULL;
        }

        if (size)
        {
            buf = new byte[(size + SymmCipher::BLOCKSIZE - 1) & - SymmCipher::BLOCKSIZE];
        }
        buflen = size;
    }
}



EncryptByChunks::EncryptByChunks(SymmCipher* k, chunkmac_map* m, uint64_t iv) : key(k), macs(m), ctriv(iv)
{
    memset(crc, 0, CRCSIZE);
}

void EncryptByChunks::updateCRC(byte* data, unsigned size, unsigned offset)
{
    uint32_t *intc = (uint32_t *)crc;

    int ol = offset % CRCSIZE;
    if (ol)
    {
        int ll = CRCSIZE - ol;
        size -= ll;
        while (ll--)
        {
            crc[ol++] ^= *data++;
        }
    }

    uint32_t *intdata = (uint32_t *)data;
    int ll = size % CRCSIZE;
    int l = size / CRCSIZE;
    if (l)
    {
        l *= 3;
        while (l)
        {
            l -= 3;
            intc[0] ^= intdata[l];
            intc[1] ^= intdata[l + 1];
            intc[2] ^= intdata[l + 2];
        }
    }
    if (ll)
    {
        data += (size - ll);
        while (ll--)
        {
            crc[ll] ^= data[ll];
        }
    }
}

bool EncryptByChunks::encrypt(m_off_t pos, m_off_t npos, string& urlSuffix)
{
    byte* buf;
    m_off_t startpos = pos;
    m_off_t finalpos = npos;
    m_off_t endpos = ChunkedHash::chunkceil(startpos, finalpos);
    m_off_t chunksize = endpos - startpos;
    while (chunksize)
    {
        byte mac[SymmCipher::BLOCKSIZE] = { 0 };
        buf = nextbuffer(unsigned(chunksize));
        if (!buf) return false;
        key->ctr_crypt(buf, unsigned(chunksize), startpos, ctriv, mac, 1);
        memcpy((*macs)[startpos].mac, mac, sizeof mac);
        (*macs)[startpos].finished = false;  // finished is only set true after confirmation of the chunk uploading.
        LOG_debug << "Encrypted chunk: " << startpos << " - " << endpos << "   Size: " << chunksize;

        updateCRC(buf, unsigned(chunksize), unsigned(startpos - pos));

        startpos = endpos;
        endpos = ChunkedHash::chunkceil(startpos, finalpos);
        chunksize = endpos - startpos;
    }
    assert(endpos == finalpos);
    buf = nextbuffer(0);   // last call in case caller does buffer post-processing (such as write to file as we go)

    ostringstream s;
    s << "/" << pos << "?c=" << Base64Str<EncryptByChunks::CRCSIZE>(crc);
    urlSuffix = s.str();

    return !!buf;
}


EncryptBufferByChunks::EncryptBufferByChunks(byte* b, SymmCipher* k, chunkmac_map* m, uint64_t iv)
    : EncryptByChunks(k, m, iv)
    , chunkstart(b)
{
}

byte* EncryptBufferByChunks::nextbuffer(unsigned bufsize)
{
    byte* pos = chunkstart;
    chunkstart += bufsize;
    return pos;
}

// prepare chunk for uploading: mac and encrypt
void HttpReqUL::prepare(const char* tempurl, SymmCipher* key,
                        uint64_t ctriv, m_off_t pos,
                        m_off_t npos)
{
    EncryptBufferByChunks eb((byte*)out->data(), key, &mChunkmacs, ctriv);

    string urlSuffix;
    eb.encrypt(pos, npos, urlSuffix);

    // unpad for POSTing
    size = (unsigned)(npos - pos);
    out->resize(size);

    setreq((tempurl + urlSuffix).c_str(), REQ_BINARY);
}

// number of bytes sent in this request
m_off_t HttpReqUL::transferred(MegaClient* client)
{
    if (httpiohandle)
    {
        return client->httpio->postpos(httpiohandle);
    }

    return 0;
}

SpeedController::SpeedController()
{
    partialBytes = 0;
    meanSpeed = 0;
    lastUpdate = 0;
    speedCounter = 0;
}

m_off_t SpeedController::calculateSpeed(long long numBytes)
{
    dstime currentTime = Waiter::ds;
    if (numBytes <= 0 && lastUpdate == currentTime)
    {
        return (partialBytes * 10) / SPEED_MEAN_INTERVAL_DS;
    }

    while (transferBytes.size())
    {
        map<dstime, m_off_t>::iterator it = transferBytes.begin();
        dstime deltaTime = currentTime - it->first;
        if (deltaTime < SPEED_MEAN_INTERVAL_DS)
        {
            break;
        }

        partialBytes -= it->second;
        transferBytes.erase(it);
    }

    if (numBytes > 0)
    {
        transferBytes[currentTime] += numBytes;
        partialBytes += numBytes;
    }

    m_off_t speed = (partialBytes * 10) / SPEED_MEAN_INTERVAL_DS;
    if (numBytes)
    {
        meanSpeed = meanSpeed * speedCounter + speed;
        speedCounter++;
        meanSpeed /= speedCounter;
        if (speedCounter > SPEED_MAX_VALUES)
        {
            speedCounter = SPEED_MAX_VALUES;
        }
    }
    lastUpdate = currentTime;
    return speed;
}

m_off_t SpeedController::getMeanSpeed()
{
    return meanSpeed;
}

GenericHttpReq::GenericHttpReq(PrnGen &rng, bool binary)
    : HttpReq(binary), bt(rng), maxbt(rng)
{
    tag = 0;
    maxretries = 0;
    numretry = 0;
    isbtactive = false;
}

} // namespace
