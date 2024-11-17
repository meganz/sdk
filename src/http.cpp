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

#if defined(WIN32)
#include <winhttp.h>
#endif

#if defined(__APPLE__) && !(TARGET_OS_IPHONE)
#include "mega/osx/osxutils.h"
#endif

#if TARGET_OS_IPHONE
#include <resolv.h>
#endif

namespace mega {

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

    lock_guard<mutex> g(g_APIURL_default_mutex);
    APIURL = g_APIURL_default;
    disablepkp = g_disablepkp_default;
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

#if defined(WIN32)
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

void HttpIO::getMEGADNSservers(string* dnsservers, bool getfromnetwork)
{
    if (!dnsservers)
    {
        return;
    }

    dnsservers->clear();
    if (getfromnetwork)
    {
        struct addrinfo* aiList = NULL;
        struct addrinfo* hp;

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
                    sockaddr_in* addr = (sockaddr_in*)hp->ai_addr;
                    mega_inet_ntop(hp->ai_family, &addr->sin_addr, straddr, sizeof(straddr));
                }
                else if (hp->ai_family == AF_INET6)
                {
                    sockaddr_in6* addr = (sockaddr_in6*)hp->ai_addr;
                    mega_inet_ntop(hp->ai_family, &addr->sin6_addr, straddr, sizeof(straddr));
                }

                if (straddr[0])
                {
                    if (dnsservers->size())
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
    LOG_verbose << "[HttpReq::HttpReq] CONSTRUCTOR CALL [this = " << this << "]";
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
    mChunked = false;

    init();
}

HttpReq::~HttpReq()
{
    LOG_verbose << "[HttpReq::~HttpReq] DESTRUCTOR CALL [this = " << this << "]";
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
    mRedirectURL.clear();
}

const char* HttpReq::getMethodString()
{
    switch(method)
    {
    case METHOD_POST:
        return "POST";
    case METHOD_GET:
        return "GET";
    case METHOD_NONE:
        return "NONE";
    default:
        return "UNKNOWN_METHOD";
    }
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

bool HttpReq::http_buf_t::isNull() const
{
    return buf == NULL;
}

byte* HttpReq::http_buf_t::datastart() const
{
    return buf + start;
}

size_t HttpReq::http_buf_t::datalen() const
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

    if (mChunked)
    {
        // Immediate purge because there are several places
        // in the code directly accesing HttpReq::in instead
        // of HttpReq::data() and HttpReq::size()
        in.erase(0, inpurge);
        inpurge = 0;
    }
}

// set total response size
void HttpReq::setcontentlength(m_off_t len)
{
    if (!buf && type != REQ_BINARY && !mChunked)
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
    if (tempurl && *tempurl)
    {
        char urlbuf[512];
        snprintf(urlbuf, sizeof urlbuf, "%s/%" PRIu64 "-%" PRIu64, tempurl, pos, npos ? npos - 1 : 0);
        setreq(urlbuf, REQ_BINARY);
    }
    else
    {
        setreq(nullptr, REQ_BINARY);
    }

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

    unsigned ol = offset % CRCSIZE;
    if (ol)
    {
        unsigned ll = CRCSIZE - ol;
        if (ll > size) //last chunks could be smaller than CRCSIZE!
        {
            ll = size;
        }
        size -= ll;
        while (ll--)
        {
            crc[ol++] ^= *data;
            ++data;
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
        buf = nextbuffer(unsigned(chunksize));
        if (!buf) return false;

        // The chunk is fully encrypted but finished==false for now,
        // we only set finished after confirmation of the chunk uploading.
        macs->ctr_encrypt(startpos, key, buf, unsigned(chunksize), startpos, ctriv, false);

        LOG_debug << "Encrypted chunk: " << startpos << " - " << endpos << "   Size: " << chunksize;

        updateCRC(buf, unsigned(chunksize), unsigned(startpos - pos));

        startpos = endpos;
        endpos = ChunkedHash::chunkceil(startpos, finalpos);
        chunksize = endpos - startpos;
    }
    assert(endpos == finalpos);
    buf = nextbuffer(0);   // last call in case caller does buffer post-processing (such as write to file as we go)

    ostringstream s;
    s << "/" << pos << "?d=" << Base64Str<EncryptByChunks::CRCSIZE>(crc);
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

GenericHttpReq::GenericHttpReq(PrnGen &rng, bool binary)
    : HttpReq(binary), bt(rng), maxbt(rng)
{
    tag = 0;
    maxretries = 0;
    numretry = 0;
    isbtactive = false;
}


/********************\
 *  SpeedController  *
\********************/

SpeedController::SpeedController()
{
    requestStarted();
}

void SpeedController::requestStarted()
{
    dstime currentTime = Waiter::ds;

    mRequestPos = 0;
    mRequestStart = mLastRequestUpdate = currentTime;

    // Increment the initial time by the time since the last circular update
    // (almost equivalent to mLastRequestUpdate). This ensures an accurate total
    // mean calculation, including previous data for the same connection.
    mInitialTime += mCircularCurrentTime ? (currentTime - mCircularCurrentTime) : currentTime;
    mCircularCurrentTime = currentTime;
}

m_off_t SpeedController::requestProgressed(m_off_t newPos)
{
    if (newPos > mRequestPos)
    {
        m_off_t delta = newPos - mRequestPos;
        calculateSpeed(delta);
        mRequestPos = newPos;
        mLastRequestUpdate = Waiter::ds;
        return delta;
    }
    return 0;
}

m_off_t SpeedController::lastRequestMeanSpeed() const
{
    // If deltaDs is 0 we consider it as 1, it won't be really accurate (the mean value will be lower than it should), but better than returning a 0
    // For example, mRequestPos = 50 bytes; deltaDs = 0 [real value = 0.5]. 50 bytes per 0.5 ds = 100 bytes per decisecond = 1000 bytes per second
    // However, as we deltaDs is an integer value truncated to 0, we would have: 50 bytes * 10 decisecondsPerSecond / 1 = 500 bytes per second.
    // Lower than it should be, but better than just returning a 0.
    dstime deltaDs = std::max<dstime>(1, mLastRequestUpdate - mRequestStart);
    return aggregateProgressForTimePeriod(DS_PER_SECOND, deltaDs, mRequestPos);
}

dstime SpeedController::requestElapsedDs() const
{
    return Waiter::ds - mRequestStart;
}

m_off_t SpeedController::getMeanSpeed() const
{
    return mMeanSpeed;
}

// Get the current circular speed by aggregating progress (from deciseconds to seconds) over the circular time period (SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS).
m_off_t SpeedController::getCircularMeanSpeed() const
{
    assert(mCircularCurrentTime >= mInitialTime);
    if (mCircularCurrentSum == 0)
    {
        // Return zero speed if circular buffer is empty.
        return 0;
    }
    dstime deltaTimeFromBeginning = std::max<dstime>(1, mCircularCurrentTime - mInitialTime); // See comment in "calculateMeanSpeed()" to understand why we do this.
    dstime totalSumTime = deltaTimeFromBeginning >= ((SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS - 1) * DS_PER_SECOND) ?
                                (((SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS - 1) * DS_PER_SECOND) + calculateCurrentSecondOffsetInDs()) : // We always have a "current/incomplete second"
                                deltaTimeFromBeginning;
    assert(totalSumTime > 0);
    return aggregateProgressForTimePeriod(DS_PER_SECOND, totalSumTime, mCircularCurrentSum);
}

// Calculate the total mean speed by aggregating progress (from deciseconds to seconds) over the total time period.
m_off_t SpeedController::calculateMeanSpeed()
{
    // Same comment than in lastRequestMeanSpeed().
    // If deltaDs is 0 we consider it as 1, it won't be really accurate (the mean value will be lower than it should), but better than returning a 0
    // For example, mRequestPos = 50 bytes; deltaDs = 0 [real value = 0.5]. 50 bytes per 0.5 ds = 100 bytes per decisecond = 1000 bytes per second
    // However, as we deltaDs is an integer value truncated to 0, we would have: 50 bytes * 10 decisecondsPerSecond / 1 = 500 bytes per second.
    // Lower than it should be, but better than just returning a 0.
    assert(mInitialTime > 0);
    dstime deltaTimeFromBeginning = std::max<dstime>(1, Waiter::ds - mInitialTime);
    return aggregateProgressForTimePeriod(DS_PER_SECOND, deltaTimeFromBeginning, mTotalSumBytes);
}

// Calculate the circular mean speed by aggregating progress (from deciseconds to seconds) over the circular time period
m_off_t SpeedController::calculateSpeed(m_off_t delta)
{
    dstime currentTime = Waiter::ds;
    if (mInitialTime == 0 || mCircularCurrentTime == 0)
    {
        // Waiter::ds wasn't initialized when SpeedController was constructed.
        if (currentTime == 0)
        {
            LOG_err << "[SpeedController::calculateSpeed] Waiter::ds is not initialized yet!!!! We cannot calculate anything!!! And we will lose this delta!!!!";
            assert(false && "Waiter::ds is not initialized yet, and it is needed for speed calculation");
            return 0;
        }
        requestStarted();
    }
    if (delta < 0)
    {
        // If delta is negative it is due to retries, failures or reconnections, so part of the requests had to start again from an earlier position
        // In this case, we can count this delta as a "zero", even if it will decrease the mean speed. We cannot really know which amount of the new progress value
        // (smaller than before, hence the negative delta) belongs to new transferred data, and how much was "lost" due to the retry.
        // So it's fine to assume it even if it will decrease the circular mean speed during some seconds.
        // The total mean speed (getMeanSpeed) will remain practically unaffected, as we will keep updating it with positive delta values right after the next call to calculateSpeed()
        LOG_warn << "[SpeedController::calculateSpeed] delta (" << delta << ") is smaller than 0 -> truncating it to 0";
        delta = 0;
    }

    dstime deltaTimeFromPreviousCall = currentTime - mCircularCurrentTime;
    assert((currentTime == mCircularCurrentTime) || (deltaTimeFromPreviousCall > 0));
    if (deltaTimeFromPreviousCall > 0)
    {
        // Check if the time difference from the previous call, converted to seconds, is within the allowed buffer size.
        if ((deltaTimeFromPreviousCall / DS_PER_SECOND) <= SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS)
        {
            updateCircularBufferWithinLimit(delta, deltaTimeFromPreviousCall);
        }
        else
        {
            updateCircularBufferWithWeightedAverageForDeltaExceedingLimit(delta, deltaTimeFromPreviousCall);
        }
    }
    else
    {
        // We are within the current decisecond, i.e, same decisecond than the one from last call.
        mCircularBuf[mCircularCurrentIndex] += delta;
    }
    // Update circular buffer and total sum used for the mean speed
    mCircularCurrentSum += delta;
    mTotalSumBytes += delta;
    mMeanSpeed = calculateMeanSpeed();
    assert(mCircularCurrentSum >= 0);
    assert(mMeanSpeed >= 0);

    return getCircularMeanSpeed();
}

// Calculate speed within circular buffer size limit
void SpeedController::updateCircularBufferWithinLimit(m_off_t delta, dstime deltaTimeFromPreviousCall)
{
    // Calculate the current second's offset in deciseconds
    dstime circularCurrentTimeOffset = calculateCurrentSecondOffsetInDs();
    // Calculate remaining deciseconds to complete the current second
    dstime circularCurrentTimeRemainingOffsetToSecond = (DS_PER_SECOND - circularCurrentTimeOffset) % DS_PER_SECOND;
    // Calculate the current delta offset to update for the current incomplete second
    // If deltaTimeFromPreviousCall is greater than circularCurrentTimeRemainingOffsetToSecond, then we will truncate the offset to that limit
    dstime currentSecondDeltaOffset = std::min<dstime>(deltaTimeFromPreviousCall, circularCurrentTimeRemainingOffsetToSecond);
    // Update circular buffer for the current incomplete second with the calculated value above
    mCircularBuf[mCircularCurrentIndex] += aggregateProgressForTimePeriod(currentSecondDeltaOffset, deltaTimeFromPreviousCall, delta);
    // Now we can update the circular current time
    mCircularCurrentTime = Waiter::ds;

    if ((deltaTimeFromPreviousCall - currentSecondDeltaOffset) > 0)
    {
        // Update circular buffer for each full second in deltaTimeFromPreviousCall
        dstime numSeconds = (deltaTimeFromPreviousCall - circularCurrentTimeRemainingOffsetToSecond) / DS_PER_SECOND;
        for (dstime i = numSeconds; i--; )
        {
            nextIndex(mCircularCurrentIndex);
            mCircularCurrentSum -= mCircularBuf[mCircularCurrentIndex];
            mCircularBuf[mCircularCurrentIndex] = aggregateProgressForTimePeriod(DS_PER_SECOND, deltaTimeFromPreviousCall, delta);
        }
        // Update circular buffer for the new current incomplete second
        nextIndex(mCircularCurrentIndex);
        mCircularCurrentSum -= mCircularBuf[mCircularCurrentIndex];
        mCircularBuf[mCircularCurrentIndex] = aggregateProgressForTimePeriod(calculateCurrentSecondOffsetInDs(), deltaTimeFromPreviousCall, delta);
    }
}

/*
 * Calculates the weighted average per second when delta exceeds circular buffer size:
 * If the time difference from the previous call exceeds the circular buffer size,
 * calculate a weighted average (per second) between the delta and deltaTimeFromPreviousCall,
 * and update each position of the circular buffer (each one corresponds to a second) with this value.
 * Note: The position for the current incomplete second should be filled weighted to the current offset in deciseconds.
*/
void SpeedController::updateCircularBufferWithWeightedAverageForDeltaExceedingLimit(m_off_t& delta, dstime deltaTimeFromPreviousCall)
{
    // Calculate aggregated delta value per second
    m_off_t aggregatedDeltaValuePerSecond = aggregateProgressForTimePeriod(DS_PER_SECOND, deltaTimeFromPreviousCall, delta);

    // Fill circular buffer with aggregatedDeltaValuePerSecond
    std::fill(mCircularBuf.begin(), mCircularBuf.end(), aggregatedDeltaValuePerSecond);

    // Calculate the number of index positions to advance in the circular buffer
    auto deltaIndexPositions = deltaTimeFromPreviousCall / DS_PER_SECOND;
    nextIndex(mCircularCurrentIndex, deltaIndexPositions);

    // Exclude the actual second from the delta calculation
    delta = (aggregatedDeltaValuePerSecond * (SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS - 1));

    // Update circular buffer for the incomplete second if present
    mCircularCurrentTime = Waiter::ds;
    dstime circularCurrentTimeOffset = calculateCurrentSecondOffsetInDs();
    if (circularCurrentTimeOffset)
    {
        // Calculate the ponderated average progress for the current second:
        // - Time to aggregate: circularCurrentTimeOffset (the decisecond offset for the current second)
        // - Total time: DS_PER_SECOND (10 deciseconds or 1 second, corresponding to aggregatedDeltaValuePerSecond)
        // - Bytes to aggregate: aggregatedDeltaValuePerSecond
        // - Result: the ponderated average progress for the decisecond time within the current second
        // Example: If aggregatedDeltaValuePerSecond = 100 KB and circularCurrentTimeOffset = 5 deciseconds,
        //          then ponderatedProgressForCurrentSecond = (5 deciseconds * 100 KB / 10 deciseconds) = 50 KB
        auto ponderatedProgressForCurrentSecond = aggregateProgressForTimePeriod(circularCurrentTimeOffset, DS_PER_SECOND, aggregatedDeltaValuePerSecond);
        mCircularBuf[mCircularCurrentIndex] = ponderatedProgressForCurrentSecond;
        delta += ponderatedProgressForCurrentSecond;
    }
    else
    {
        mCircularBuf[mCircularCurrentIndex] = 0; // Current second (with no offset) starts from 0
    }
    mCircularCurrentSum = 0; // Reset the current sum (it must be updated with the current delta value) after calling this method
}

// Calculate offset in deciseconds for the current second starting from the initial time
dstime SpeedController::calculateCurrentSecondOffsetInDs() const
{
    return (mCircularCurrentTime - mInitialTime) % DS_PER_SECOND;
}

void SpeedController::nextIndex(size_t &currentCircularBufIndex, size_t positionsToAdvance) const
{
    currentCircularBufIndex = (currentCircularBufIndex + positionsToAdvance) % SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS;
}

/*
 * Aggregate instantaneous delta values over a specified time subperiod to calculate a weighted average.
 *
 * Calculates the total progress over the given time period by aggregating
 * the provided delta values, considering the total time and bytes to aggregate.
 *
 * @param timePeriodToAggregate The duration of the time subperiod in deciseconds.
 * @param totalTime The total duration of the time period in deciseconds.
 * @param bytesToAggregate The delta value to aggregate over the time period.
 * @return The aggregated progress over the specified time period.
 *
 * Example:
 * If 200 bytes correspond to 20 deciseconds (2 seconds) and are aggregated
 * over a period of 10 deciseconds (1 second), the calculation would be:
 * (10 * 200) / 20 = 100 bytes per second.
 */
m_off_t SpeedController::aggregateProgressForTimePeriod(dstime timePeriodToAggregate, dstime totalTime, m_off_t bytesToAggregate) const
{
    if (timePeriodToAggregate <= 0 || totalTime <= 0)
    {
        return 0;
    }
    return (timePeriodToAggregate * bytesToAggregate) / totalTime;
}

} // namespace
