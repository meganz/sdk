/**
 * @file posix/net.cpp
 * @brief POSIX network access layer (using cURL)
 *
 * (c) 2013-2017 by Mega Limited, Auckland, New Zealand
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
#include "mega/hashcash.h"
#include "mega/logging.h"
#include "mega/posix/meganet.h"

#if defined(USE_OPENSSL)
#include <openssl/err.h>
#endif

#define IPV6_RETRY_INTERVAL_DS 72000
#define DNS_CACHE_TIMEOUT_DS 18000
#define DNS_CACHE_EXPIRES 0
#define MAX_SPEED_CONTROL_TIMEOUT_MS 500

namespace mega {

std::atomic<bool> g_netLoggingOn{false};

#define NET_verbose if (g_netLoggingOn) LOG_verbose
#define NET_debug if (g_netLoggingOn) LOG_debug


#if defined(_WIN32)

HANDLE SockInfo::sharedEventHandle()
{
    return mSharedEvent;
}

bool SockInfo::createAssociateEvent()
{
    int events = (mode & SockInfo::READ ? FD_READ : 0) | (mode & SockInfo::WRITE ? FD_WRITE : 0);

    if (associatedHandleEvents != events)
    {
        if (WSAEventSelect(fd, mSharedEvent, events))
        {
            auto err = WSAGetLastError();
            LOG_err << "WSAEventSelect failed " << fd << " " << mSharedEvent << " " << events << " " << err;
            closeEvent();
            return false;
        }
        associatedHandleEvents = events;
    }
    return true;
}

bool SockInfo::checkEvent(bool& read, bool& write, bool logErr)
{
    WSANETWORKEVENTS wne;
    memset(&wne, 0, sizeof(wne));
    auto err = WSAEnumNetworkEvents(fd, NULL, &wne);
    if (err)
    {
        if (logErr)
        {
            auto e = WSAGetLastError();
            LOG_err << "WSAEnumNetworkEvents error " << e;
        }
        return false;
    }

    read = 0 != (FD_READ & wne.lNetworkEvents);
    write = 0 != (FD_WRITE & wne.lNetworkEvents);

    // Even though the writeable network event occurred, double check there is no space available in the write buffer
    // Otherwise curl can report a spurious timeout error

    if (FD_WRITE & associatedHandleEvents)
    {
        // per https://curl.haxx.se/mail/lib-2009-10/0313.html check if the socket has any buffer space

        // The trick is that we want to wait on the event handle to know when we can read and write
        // that works fine for read, however for write the event is not signalled in the normal case
        // where curl wrote to the socket, but not enough to cause it to become unwriteable for now.
        // So, we need to signal curl to write again if it has more data to write, if the socket can take
        // more data.  This trick with WSASend for 0 bytes enables that - if it fails with would-block
        // then we can stop asking curl to write to the socket, and start waiting on the handle to
        // know when to try again.
        // If curl has finished writing to the socket, it will call us back to change the mode to read only.

        WSABUF buf{ 0, (CHAR*)&buf };
        DWORD bSent = 0;
        auto writeResult = WSASend(fd, &buf, 1, &bSent, 0, NULL, NULL);
        auto writeError = WSAGetLastError();
        write = writeResult == 0 || (writeError != WSAEWOULDBLOCK && writeError != WSAENOTCONN);
        if (writeResult != 0 && writeError != WSAEWOULDBLOCK && writeError != WSAENOTCONN)
        {
            LOG_err << "Unexpected WSASend check error: " << writeError;
        }
    }

    if (read || write)
    {
        signalledWrite = signalledWrite || write;
        return true;   // if we return true, both read and write must have been set.
    }
    return false;
}

void SockInfo::closeEvent(bool adjustSocket)
{
    if (adjustSocket)
    {
        int result = WSAEventSelect(fd, NULL, 0); // cancel association by specifying lNetworkEvents = 0
        if (result)
        {
            auto err = WSAGetLastError();
            LOG_err << "WSAEventSelect error: " << err;
        }
    }
    associatedHandleEvents = 0;
    signalledWrite = false;
}

SockInfo::SockInfo(SockInfo&& o)
    : fd(o.fd)
    , mode(o.mode)
    , signalledWrite(o.signalledWrite)
    , mSharedEvent(o.mSharedEvent)
    , associatedHandleEvents(o.associatedHandleEvents)
{
}

SockInfo::~SockInfo()
{
}
#endif

std::mutex CurlHttpIO::curlMutex;

#if defined(USE_OPENSSL) && !defined(OPENSSL_IS_BORINGSSL)

std::recursive_mutex **CurlHttpIO::sslMutexes = NULL;
static std::mutex lock_init_mutex;
void CurlHttpIO::locking_function(int mode, int lockNumber, const char *, int)
{
    std::recursive_mutex *mutex = sslMutexes[lockNumber];
    if (mutex == NULL)
    {
        // we still have to be careful about multiple threads getting to this point simultaneously
        lock_init_mutex.lock();
        mutex = sslMutexes[lockNumber];
        if (!mutex)
        {
            mutex = sslMutexes[lockNumber] = new std::recursive_mutex;
        }
        lock_init_mutex.unlock();
    }

    if (mode & CRYPTO_LOCK)
    {
        mutex->lock();
    }
    else
    {
        mutex->unlock();
    }
}

#if OPENSSL_VERSION_NUMBER >= 0x10000000 || defined (LIBRESSL_VERSION_NUMBER)
void CurlHttpIO::id_function([[maybe_unused]] CRYPTO_THREADID* id)
{
    CRYPTO_THREADID_set_pointer(id, (void *)THREAD_CLASS::currentThreadId());
}
#else
unsigned long CurlHttpIO::id_function()
{
    return THREAD_CLASS::currentThreadId();
}
#endif

#endif

CurlHttpIO::CurlHttpIO()
{
#ifdef WIN32
    mSocketsWaitEvent = WSACreateEvent();
    if (mSocketsWaitEvent == WSA_INVALID_EVENT)
    {
        LOG_err << "Failed to create WSA event for cURL";
    }
#endif

    curl_version_info_data* data = curl_version_info(CURLVERSION_NOW);
    if (data->version)
    {
        LOG_debug << "curl version: " << data->version;
    }

    if (data->ssl_version)
    {
        LOG_debug << "SSL version: " << data->ssl_version;

        string curlssl = data->ssl_version;
        tolower_string(curlssl);
        if (strstr(curlssl.c_str(), "gskit"))
        {
            LOG_fatal << "Unsupported SSL backend (GSKit). Aborting.";
            throw std::runtime_error("Unsupported SSL backend (GSKit). Aborting.");
        }

        if (data->version_num < 0x072c00 // At least curl 7.44.0
#ifdef USE_OPENSSL
            && !(strstr(curlssl.c_str(), "openssl") && data->version_num > 0x070b00)
        // or curl 7.11.0 with OpenSSL
#endif
        )
        {
            LOG_fatal << "curl built without public key pinning support. Aborting.";
            throw std::runtime_error("curl built without public key pinning support. Aborting.");
        }
    }

    if (data->libz_version)
    {
        LOG_debug << "libz version: " << data->libz_version;
    }

    if (data->zstd_version)
    {
        LOG_debug << "zstd version: " << data->zstd_version;
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
        LOG_fatal << "curl built without HTTP/HTTPS support. Aborting.";
        throw std::runtime_error("curl built without HTTP/HTTPS support. Aborting.");
    }

    if (data->ares)
    {
        int version{data->ares_num};
        int major{(version >> 16) & 0xFF};
        int minor{(version >> 8) & 0xFF};
        int patch{version & 0xFF};
        LOG_debug << "curl built with c-ares backend as DNS resolver.";
        LOG_debug << "c-ares version: " << major << "." << minor << "." << patch;
    }

    curlipv6 = data->features & CURL_VERSION_IPV6;
    LOG_debug << "IPv6 enabled: " << curlipv6;

    dnsok = false;
    reset = false;
    statechange = false;
    disconnecting = false;
    maxspeed[GET] = 0;
    maxspeed[PUT] = 0;
    pkpErrors = 0;

    WAIT_CLASS::bumpds();
    lastdnspurge = Waiter::ds + DNS_CACHE_TIMEOUT_DS / 2;

    curlMutex.lock();

#if defined(USE_OPENSSL) && !defined(OPENSSL_IS_BORINGSSL)

    // It's needed to check if sslMutexes have been already initialized because
    // in OpenSSL versions >= 1.1.0 these mutexes are not needed anymore and
    // CRYPTO_get_locking_callback() always returns NULL.
    // OPENSSL_VERSION_NUMBER could be used to skip this initialization, but
    // since there are so many implementations of OpenSSL, I think that it's
    // safer to provide the mutexes even if they are not really needed.
    if (!CRYPTO_get_locking_callback() && !sslMutexes
#if OPENSSL_VERSION_NUMBER >= 0x10000000  || defined (LIBRESSL_VERSION_NUMBER)
        && !CRYPTO_THREADID_get_callback())
#else
        && !CRYPTO_get_id_callback())
#endif
    {
        LOG_debug << "Initializing OpenSSL locking callbacks";
        size_t numLocks = CRYPTO_num_locks();
        sslMutexes = new std::recursive_mutex*[numLocks];
        memset(sslMutexes, 0, numLocks * sizeof(std::recursive_mutex*));
#if OPENSSL_VERSION_NUMBER >= 0x10000000  || defined (LIBRESSL_VERSION_NUMBER)
        ((void)(CRYPTO_THREADID_set_callback(CurlHttpIO::id_function)));
#else
        CRYPTO_set_id_callback(CurlHttpIO::id_function);
#endif
        CRYPTO_set_locking_callback(CurlHttpIO::locking_function);
    }

#endif

    if (++instanceCount == 1)
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    };

    curlMutex.unlock();

    curlm[API] = curl_multi_init();
    curlm[GET] = curl_multi_init();
    curlm[PUT] = curl_multi_init();
    numconnections[API] = 0;
    numconnections[GET] = 0;
    numconnections[PUT] = 0;

    curl_multi_setopt(curlm[API], CURLMOPT_SOCKETFUNCTION, api_socket_callback);
    curl_multi_setopt(curlm[API], CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(curlm[API], CURLMOPT_TIMERFUNCTION, api_timer_callback);
    curl_multi_setopt(curlm[API], CURLMOPT_TIMERDATA, this);
    curltimeoutreset[API] = -1;
    arerequestspaused[API] = false;

    curl_multi_setopt(curlm[GET], CURLMOPT_SOCKETFUNCTION, download_socket_callback);
    curl_multi_setopt(curlm[GET], CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(curlm[GET], CURLMOPT_TIMERFUNCTION, download_timer_callback);
    curl_multi_setopt(curlm[GET], CURLMOPT_TIMERDATA, this);
#ifdef _WIN32
    curl_multi_setopt(curlm[GET], CURLMOPT_MAXCONNECTS, 200);
#endif
    curltimeoutreset[GET] = -1;
    arerequestspaused[GET] = false;

    curl_multi_setopt(curlm[PUT], CURLMOPT_SOCKETFUNCTION, upload_socket_callback);
    curl_multi_setopt(curlm[PUT], CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(curlm[PUT], CURLMOPT_TIMERFUNCTION, upload_timer_callback);
    curl_multi_setopt(curlm[PUT], CURLMOPT_TIMERDATA, this);
#ifdef _WIN32
    curl_multi_setopt(curlm[PUT], CURLMOPT_MAXCONNECTS, 200);
#endif

    curltimeoutreset[PUT] = -1;
    arerequestspaused[PUT] = false;

    curlsh = curl_share_init();
    curl_share_setopt(curlsh, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(curlsh, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);

    contenttypejson = curl_slist_append(NULL, "Content-Type: application/json");
    contenttypejson = curl_slist_append(contenttypejson, "Expect:");

    contenttypebinary = curl_slist_append(NULL, "Content-Type: application/octet-stream");
    contenttypebinary = curl_slist_append(contenttypebinary, "Expect:");

    proxyinflight = 0;
    ipv6requestsenabled = false;
    ipv6proxyenabled = ipv6requestsenabled;
    ipv6deactivationtime = Waiter::ds;
    waiter = NULL;
    proxyport = 0;
    proxytype = Proxy::NONE;
}

bool CurlHttpIO::ipv6available()
{
    static int ipv6_works = -1;

    if (ipv6_works != -1)
    {
        return ipv6_works != 0;
    }

    curl_socket_t s = socket(PF_INET6, SOCK_DGRAM, 0);

    if (s == -1)
    {
        ipv6_works = 0;
    }
    else
    {
        ipv6_works = curlipv6;
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
    }

    return ipv6_works != 0;
}

void CurlHttpIO::addcurlevents(Waiter* eventWaiter, direction_t d)
{
#ifdef MEGA_MEASURE_CODE
    CodeCounter::ScopeTimer ccst(countAddCurlEventsCode);
#endif

#if defined(_WIN32)
    bool anyWriters = false;
#endif

    SockInfoMap &socketmap = curlsockets[d];
    for (SockInfoMap::iterator it = socketmap.begin(); it != socketmap.end(); it++)
    {
        SockInfo &info = it->second;
        if (!info.mode)
        {
            continue;
        }

#if defined(_WIN32)
        anyWriters = anyWriters || info.signalledWrite;
        info.signalledWrite = false;
        info.createAssociateEvent();
#else

        if (info.mode & SockInfo::READ)
        {
            MEGA_FD_SET(info.fd, &((PosixWaiter*)eventWaiter)->rfds);
            ((PosixWaiter*)eventWaiter)->bumpmaxfd(info.fd);
        }

        if (info.mode & SockInfo::WRITE)
        {
            MEGA_FD_SET(info.fd, &((PosixWaiter*)eventWaiter)->wfds);
            ((PosixWaiter*)eventWaiter)->bumpmaxfd(info.fd);
        }
#endif
   }

#if defined(_WIN32)
    if (anyWriters)
    {
        // so long as we are writing at least one socket, keep looping until the socket is full, then start waiting on its associated event
        static_cast<WinWaiter*>(eventWaiter)->maxds = 0;
    }
#endif
}

int CurlHttpIO::checkevents(Waiter*)
{
#ifdef WIN32
    // if this assert triggers, it means that we detected that cURL needs to be called,
    // and it was not called.  Since we reset the event, we don't get another chance.
    assert(!mSocketsWaitEvent_curl_call_needed);
    bool wasSet = WAIT_OBJECT_0 == WaitForSingleObject(mSocketsWaitEvent, 0);
    mSocketsWaitEvent_curl_call_needed = wasSet;
    ResetEvent(mSocketsWaitEvent);
    return wasSet ? Waiter::NEEDEXEC : 0;
#else
    return 0;
#endif
}

void CurlHttpIO::closecurlevents(direction_t d)
{
    SockInfoMap &socketmap = curlsockets[d];
#if defined(_WIN32)
    for (SockInfoMap::iterator it = socketmap.begin(); it != socketmap.end(); it++)
    {
        it->second.closeEvent(false);
    }
#endif
    socketmap.clear();
}

void CurlHttpIO::processcurlevents(direction_t d)
{
#ifdef MEGA_MEASURE_CODE
    CodeCounter::ScopeTimer ccst(countProcessCurlEventsCode);
#endif

#ifdef WIN32
    mSocketsWaitEvent_curl_call_needed = false;
#else
    auto *rfds = &((PosixWaiter *)waiter)->rfds;
    auto *wfds = &((PosixWaiter *)waiter)->wfds;
#endif

    int dummy = 0;
    SockInfoMap *socketmap = &curlsockets[d];
    bool *paused = &arerequestspaused[d];

    for (SockInfoMap::iterator it = socketmap->begin(); !(*paused) && it != socketmap->end();)
    {
        SockInfo &info = (it++)->second;
        if (!info.mode)
        {
            continue;
        }

#if defined(_WIN32)
        bool read, write;
        if (info.checkEvent(read, write)) // if checkEvent returns true, both `read` and `write` have been set.
        {
            //LOG_verbose << "Calling curl for socket " << info.fd << (read && write ? " both" : (read ? " read" : " write"));
            curl_multi_socket_action(curlm[d], info.fd,
                                     (read ? CURL_CSELECT_IN : 0)
                                   | (write ? CURL_CSELECT_OUT : 0), &dummy);
        }
#else
        if (((info.mode & SockInfo::READ) && MEGA_FD_ISSET(info.fd, rfds)) || ((info.mode & SockInfo::WRITE) && MEGA_FD_ISSET(info.fd, wfds)))
        {
            curl_multi_socket_action(curlm[d], info.fd,
                                     (((info.mode & SockInfo::READ) && MEGA_FD_ISSET(info.fd, rfds)) ? CURL_CSELECT_IN : 0)
                                     | (((info.mode & SockInfo::WRITE) && MEGA_FD_ISSET(info.fd, wfds)) ? CURL_CSELECT_OUT : 0),
                                     &dummy);
        }
#endif
    }

    if (curltimeoutreset[d] >= 0 && curltimeoutreset[d] <= Waiter::ds)
    {
        curltimeoutreset[d] = -1;
        NET_debug << "Informing cURL of timeout reached for " << d << " at " << Waiter::ds;
        curl_multi_socket_action(curlm[d], CURL_SOCKET_TIMEOUT, 0, &dummy);
    }

    for (SockInfoMap::iterator it = socketmap->begin(); it != socketmap->end();)
    {
        SockInfo &info = it->second;
        if (!info.mode)
        {
            socketmap->erase(it++);
        }
        else
        {
            it++;
        }
    }
}

CurlHttpIO::~CurlHttpIO()
{
    disconnecting = true;
    curl_multi_cleanup(curlm[API]);
    curl_multi_cleanup(curlm[GET]);
    curl_multi_cleanup(curlm[PUT]);
    curl_share_cleanup(curlsh);

    closecurlevents(API);
    closecurlevents(GET);
    closecurlevents(PUT);

#ifdef WIN32
    WSACloseEvent(mSocketsWaitEvent);
#endif

    curlMutex.lock();
    if (--instanceCount == 0)
    {
        curl_global_cleanup();
    }
    curlMutex.unlock();

    curl_slist_free_all(contenttypejson);
    curl_slist_free_all(contenttypebinary);
}

int CurlHttpIO::instanceCount = 0;

void CurlHttpIO::setuseragent(string* u)
{
    useragent = *u;
}

bool CurlHttpIO::setdnsservers(const char* servers)
{
    const curl_version_info_data* data = curl_version_info(CURLVERSION_NOW);

    if (!data->ares)
    {
        return false;
    }

    if (servers)
    {
        lastdnspurge = Waiter::ds + DNS_CACHE_TIMEOUT_DS / 2;
        if (DNS_CACHE_EXPIRES)
        {
            dnscache.clear();
        }

        dnsservers = servers;
        LOG_debug << "Setting custom DNS servers: " << dnsservers;
    }
    return true;
}

void CurlHttpIO::disconnect()
{
    LOG_debug << "Reinitializing the network layer";
    disconnecting = true;
    assert(!numconnections[API] && !numconnections[GET] && !numconnections[PUT]);

    curl_multi_cleanup(curlm[API]);
    curl_multi_cleanup(curlm[GET]);
    curl_multi_cleanup(curlm[PUT]);

    if (numconnections[API] || numconnections[GET] || numconnections[PUT])
    {
        LOG_err << "Disconnecting without cancelling all requests first";
        numconnections[API] = 0;
        numconnections[GET] = 0;
        numconnections[PUT] = 0;
    }

    closecurlevents(API);
    closecurlevents(GET);
    closecurlevents(PUT);

    lastdnspurge = Waiter::ds + DNS_CACHE_TIMEOUT_DS / 2;
    if (DNS_CACHE_EXPIRES)
    {
        dnscache.clear();
    }
    else
    {
        for (auto &dnsPair: dnscache)
        {
            dnsPair.second.mNeedsResolvingAgain= true;
        }
    }

    curlm[API] = curl_multi_init();
    curlm[GET] = curl_multi_init();
    curlm[PUT] = curl_multi_init();
    curl_multi_setopt(curlm[API], CURLMOPT_SOCKETFUNCTION, api_socket_callback);
    curl_multi_setopt(curlm[API], CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(curlm[API], CURLMOPT_TIMERFUNCTION, api_timer_callback);
    curl_multi_setopt(curlm[API], CURLMOPT_TIMERDATA, this);
    curltimeoutreset[API] = -1;
    arerequestspaused[API] = false;

    curl_multi_setopt(curlm[GET], CURLMOPT_SOCKETFUNCTION, download_socket_callback);
    curl_multi_setopt(curlm[GET], CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(curlm[GET], CURLMOPT_TIMERFUNCTION, download_timer_callback);
    curl_multi_setopt(curlm[GET], CURLMOPT_TIMERDATA, this);
#ifdef _WIN32
    curl_multi_setopt(curlm[GET], CURLMOPT_MAXCONNECTS, 200);
#endif
    curltimeoutreset[GET] = -1;
    arerequestspaused[GET] = false;


    curl_multi_setopt(curlm[PUT], CURLMOPT_SOCKETFUNCTION, upload_socket_callback);
    curl_multi_setopt(curlm[PUT], CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(curlm[PUT], CURLMOPT_TIMERFUNCTION, upload_timer_callback);
    curl_multi_setopt(curlm[PUT], CURLMOPT_TIMERDATA, this);
#ifdef _WIN32
    curl_multi_setopt(curlm[PUT], CURLMOPT_MAXCONNECTS, 200);
#endif
    curltimeoutreset[PUT] = -1;
    arerequestspaused[PUT] = false;

    disconnecting = false;
    if (proxyurl.size() && !proxyip.size())
    {
        LOG_debug << "Unresolved proxy name. Resolving...";
        request_proxy_ip();
    }
}

bool CurlHttpIO::setmaxdownloadspeed(m_off_t bpslimit)
{
    LOG_debug << "[CurlHttpIO::setmaxdownloadspeed] Set max download speed to " << bpslimit
              << " B/s";
    maxspeed[GET] = bpslimit;
    return true;
}

bool CurlHttpIO::setmaxuploadspeed(m_off_t bpslimit)
{
    LOG_debug << "[CurlHttpIO::setmaxuploadspeed] Set max upload speed to " << bpslimit << " B/s";
    maxspeed[PUT] = bpslimit;
    return true;
}

m_off_t CurlHttpIO::getmaxdownloadspeed()
{
    return maxspeed[GET];
}

m_off_t CurlHttpIO::getmaxuploadspeed()
{
    return maxspeed[PUT];
}

bool CurlHttpIO::cacheresolvedurls(const std::vector<string>& urls, std::vector<string>&& ips)
{
    // for each URL there should be 2 IPs (IPv4 first, IPv6 second)
    if (urls.empty() || urls.size() * 2 != ips.size())
    {
        LOG_err << "Resolved URLs to be cached did not match with an IPv4 and IPv6 each";
        return false;
    }

    for (std::vector<string>::size_type i = 0; i < urls.size(); ++i)
    {
        // get host name from each URL
        string host, dummyscheme;
        int dummyPort;
        const string& url = urls[i]; // this is "free" and helps with debugging

        crackurl(&url, &dummyscheme, &host, &dummyPort);

        // add resolved host name to cache, or replace the previous one
        CurlDNSEntry& dnsEntry = dnscache[host];
        dnsEntry.ipv4 = std::move(ips[2 * i]);
        dnsEntry.ipv4timestamp = Waiter::ds;
        dnsEntry.ipv6 = std::move(ips[2 * i + 1]);
        dnsEntry.ipv6timestamp = Waiter::ds;
        dnsEntry.mNeedsResolvingAgain = false;
    }

    return true;
}

// wake up from cURL I/O
void CurlHttpIO::addevents(Waiter* w, int)
{
#ifdef MEGA_MEASURE_CODE
    CodeCounter::ScopeTimer ccst(countCurlHttpIOAddevents);
#endif

    waiter = (WAIT_CLASS*)w;
    long curltimeoutms = -1;

    addcurlevents(waiter, API);

#ifdef WIN32
    ((WinWaiter*)waiter)->addhandle(mSocketsWaitEvent, Waiter::NEEDEXEC);
#endif

    if (curltimeoutreset[API] >= 0)
    {
        m_time_t ds = curltimeoutreset[API] - Waiter::ds;
        if (ds <= 0)
        {
            curltimeoutms = 0;
        }
        else
        {
            if (curltimeoutms < 0 || curltimeoutms > ds * 100)
            {
                curltimeoutms = long(ds * 100);
            }
        }
    }

    for (int d = GET; d == GET || d == PUT; d += PUT - GET)
    {
        if (arerequestspaused[d])
        {
            if (curltimeoutms < 0 || curltimeoutms > 100)
            {
                curltimeoutms = 100;
            }
        }
        else
        {
            addcurlevents(waiter, (direction_t)d);
            if (curltimeoutreset[d] >= 0)
            {
                m_time_t ds = curltimeoutreset[d] - Waiter::ds;
                if (ds <= 0)
                {
                    curltimeoutms = 0;
                }
                else
                {
                    if (curltimeoutms < 0 || curltimeoutms > ds * 100)
                    {
                        curltimeoutms = long(ds * 100);
                    }
                }
            }
        }
    }

    if ((curltimeoutms < 0 || curltimeoutms > MAX_SPEED_CONTROL_TIMEOUT_MS) &&
        (downloadSpeed || uploadSpeed))
    {
        curltimeoutms = MAX_SPEED_CONTROL_TIMEOUT_MS;
    }

    if (curltimeoutms >= 0)
    {
        m_time_t timeoutds = curltimeoutms / 100;
        if (curltimeoutms % 100)
        {
            timeoutds++;
        }

        if (timeoutds < waiter->maxds)
        {
            waiter->maxds = dstime(timeoutds);
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

const char* CurlHttpIO::pubkeyForUrl(const char* url) const
{
    if (Utils::startswith(url, APIURL.c_str()) ||
        Utils::startswith(url, MegaClient::REQSTATURL.c_str()))
    {
        return "sha256//0W38e765pAfPqS3DqSVOrPsC4MEOvRBaXQ7nY1AJ47E=;" // API 1
               "sha256//gSRHRu1asldal0HP95oXM/5RzBfP1OIrPjYsta8og80="; // API 2
    }
    else if (Utils::startswith(url, MegaClient::SFUSTATSURL.c_str()))
    {
        return "sha256//2ZAltznnzY3Iee3NIZPOgqIQVNXVjvDEjWTmAreYVFU=;" // STATSSFU  1
               "sha256//7jLrvaEtfqTCHew0iibvEm2k61iatru+rwhFD7g3nxA="; // STATSSFU  2
    }
    return nullptr;
}
void CurlHttpIO::send_request(CurlHttpContext* httpctx)
{
    CurlHttpIO* httpio = httpctx->httpio;
    HttpReq* req = httpctx->req;
    auto len = httpctx->len;
    const char* data = httpctx->data;

    LOG_debug << httpctx->req->getLogName() << req->getMethodString()
              << " target URL: " << getSafeUrl(req->posturl);

    if (req->binary)
    {
        LOG_debug << httpctx->req->getLogName() << "[sending " << (data ? len : req->out->size())
                  << " bytes of raw data]";
    }
    else
    {
        if (gLogJSONRequests || req->out->size() < size_t(SimpleLogger::getMaxPayloadLogSize()))
        {
            LOG_debug << httpctx->req->getLogName() << "Sending " << req->out->size() << ": "
                      << DirectMessage(req->out->c_str(), req->out->size())
                      << " (at ds: " << Waiter::ds << ")";
        }
        else
        {
            LOG_debug
                << httpctx->req->getLogName() << "Sending " << req->out->size() << ": "
                << DirectMessage(req->out->c_str(),
                                 static_cast<size_t>(SimpleLogger::getMaxPayloadLogSize() / 2))
                << " [...] "
                << DirectMessage(req->out->c_str() + req->out->size() -
                                     SimpleLogger::getMaxPayloadLogSize() / 2,
                                 static_cast<size_t>(SimpleLogger::getMaxPayloadLogSize() / 2));
        }
    }

    req->outpos = 0;

    httpctx->headers = clone_curl_slist(req->type == REQ_JSON ? httpio->contenttypejson : httpio->contenttypebinary);
    httpctx->posturl = req->posturl;

    if (!req->mHashcashToken.empty())
    {
        const auto nextValue =
            gencash(req->mHashcashToken, req->mHashcashEasiness, req->mCancelSnapshot);
        string xHashcashHeader{"X-Hashcash: 1:" + req->mHashcashToken + ":" + std::move(nextValue)};
        httpctx->headers = curl_slist_append(httpctx->headers, xHashcashHeader.c_str());
        LOG_warn << httpctx->req->getLogName() << "X-Hashcash computed: " << xHashcashHeader;
        req->mHashcashToken.clear();
    }

    CURL* curl;
    curl = curl_easy_init();
    if (curl)
    {
        switch (req->method)
        {
        case METHOD_POST:
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data ? len : req->out->size());
            break;
        case METHOD_GET:
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            break;
        case METHOD_NONE:
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
            break;
        }

        if (req->timeoutms)
        {
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, req->timeoutms);
        }

        curl_easy_setopt(curl, CURLOPT_URL, httpctx->posturl.c_str());
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_data);
        curl_easy_setopt(curl, CURLOPT_READDATA, (void*)req);
        curl_easy_setopt(curl, CURLOPT_SEEKFUNCTION, seek_data);
        curl_easy_setopt(curl, CURLOPT_SEEKDATA, (void*)req);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, httpio->useragent.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, httpctx->headers);
        curl_easy_setopt(curl, CURLOPT_ENCODING, "");
        curl_easy_setopt(curl, CURLOPT_SHARE, httpio->curlsh);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)req);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, check_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void*)req);
        curl_easy_setopt(curl, CURLOPT_PRIVATE, (void*)req);
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, true);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, HttpIO::CONNECTTIMEOUT / 10);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE,  90L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
        curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, sockopt_callback);
        curl_easy_setopt(curl, CURLOPT_SOCKOPTDATA, (void*)req);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_QUICK_EXIT, 1L);

        // Some networks (eg vodafone UK) seem to block TLS 1.3 ClientHello.  1.2 is secure, and works:
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2 | CURL_SSLVERSION_MAX_TLSv1_2);

        if (httpio->maxspeed[GET] && httpio->maxspeed[GET] <= 102400)
        {
            LOG_debug << "Low maxspeed, set curl buffer size to 4 KB";
            curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 4096L);
        }

        if (req->minspeed)
        {
            LOG_debug << "Setting low speed limit (<30 Bytes/s) and how much time the speed is allowed to be lower than the limit before aborting (30 secs)";
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 30L);
        }

        if (!httpio->disablepkp && req->protect)
        {
        #if LIBCURL_VERSION_NUM >= 0x072c00 // At least cURL 7.44.0
            if (curl_easy_setopt(curl,
                                 CURLOPT_PINNEDPUBLICKEY,
                                 httpio->pubkeyForUrl(req->posturl)) == CURLE_OK)
            {
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
                if (httpio->pkpErrors)
                {
                    curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L);
                }
            }
            else
        #endif
            {
            #ifdef USE_OPENSSL // options only available for OpenSSL
                if (curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, ssl_ctx_function) != CURLE_OK)
                {
                    LOG_err << "Could not set curl option CURLOPT_SSL_CTX_FUNCTION";
                }
                if (curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, (void*)req) != CURLE_OK)
                {
                    LOG_err << "Could not set curl option CURLOPT_SSL_CTX_DATA";
                }
            #else
                LOG_fatal << "cURL built without support for public key pinning. Aborting.";
                throw std::runtime_error("ccURL built without support for public key pinning. Aborting.");
            #endif

                if (curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1) != CURLE_OK)
                {
                    LOG_err << "Could not set curl option CURLOPT_SSL_VERIFYPEER";
                }
            }
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
            if (httpio->disablepkp)
            {
                LOG_warn << "Public key pinning disabled.";
            }
        }

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
        curl_easy_setopt(curl, CURLOPT_CAINFO, NULL);
        curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);

        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, debug_callback);
        curl_easy_setopt(curl, CURLOPT_DEBUGDATA, (void*)req);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

        if (httpio->proxyip.size())
        {
            if(!httpio->proxyscheme.size() || !httpio->proxyscheme.compare(0, 4, "http"))
            {
                LOG_debug << "Using HTTP proxy";
                curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
            }
            else if(!httpio->proxyscheme.compare(0, 5, "socks"))
            {
                LOG_debug << "Using SOCKS proxy";
                curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
            }
            else
            {
                LOG_warn << "Unknown proxy type";
            }

            curl_easy_setopt(curl, CURLOPT_PROXY, httpio->proxyip.c_str());
            curl_easy_setopt(curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);

            if (httpio->proxyusername.size())
            {
                LOG_debug << "Using proxy authentication " << httpio->proxyusername.size() << " " << httpio->proxypassword.size();
                curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME, httpio->proxyusername.c_str());
                curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD, httpio->proxypassword.c_str());
            }
            else
            {
                LOG_debug << "NOT using proxy authentication";
            }

            if(httpctx->port == 443)
            {
                curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
            }
        }
        else if (httpio->proxytype == Proxy::NONE)
        {
            curl_easy_setopt(curl, CURLOPT_PROXY, "");
        }

        if (!httpio->dnsservers.empty())
        {
            curl_easy_setopt(curl, CURLOPT_DNS_SERVERS, httpio->dnsservers.c_str());
        }

        if (!httpio->proxyip.size() && httpctx->isCachedIp)
        {
            httpio->addDnsResolution(httpctx->mCurlDnsList,
                                     httpctx->hostname,
                                     httpctx->hostip,
                                     httpctx->port);
            curl_easy_setopt(curl, CURLOPT_RESOLVE, httpctx->mCurlDnsList.get());
        }

        if (DNS_CACHE_EXPIRES)
        {
            curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, DNS_CACHE_TIMEOUT_DS / 10);
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, -1);
        }

        httpio->numconnections[httpctx->d]++;
        curl_multi_add_handle(httpio->curlm[httpctx->d], curl);
        httpctx->curl = curl;
    }
    else
    {
        req->status = REQ_FAILURE;
        req->httpiohandle = NULL;
        curl_slist_free_all(httpctx->headers);

        httpctx->req = NULL;
        delete httpctx;
    }

    httpio->statechange = true;
}

void CurlHttpIO::request_proxy_ip()
{
    if (!proxyhost.empty() && proxyport)
    {
        // No need to resolve the proxy's IP: cURL will resolve it for us.
        std::ostringstream ostream;
        ostream << proxyhost << ":" << proxyport;
        proxyip = ostream.str();
    }
}

bool CurlHttpIO::crackurl(const string* url, string* scheme, string* hostname, int* port)
{
    if (!url || !url->size() || !scheme || !hostname || !port)
    {
        return false;
    }

    *port = 0;
    scheme->clear();
    hostname->clear();

    size_t starthost, endhost = 0, startport, endport;

    starthost = url->find("://");

    if (starthost != string::npos)
    {
        *scheme = url->substr(0, starthost);
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
            for (size_t i = startport; i < endport; i++)
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
        if (!scheme->compare("https"))
        {
            *port = 443;
        }
        else if (!scheme->compare("http"))
        {
            *port = 80;
        }
        else if (!scheme->compare(0, 5, "socks"))
        {
            *port = 1080;
        }
        else
        {
            *port = -1;
        }
    }

    *hostname = url->substr(starthost, endhost - starthost);

    if (*port <= 0 || starthost == string::npos || starthost >= endhost)
    {
        *port = 0;
        scheme->clear();
        hostname->clear();
        return false;
    }

    return true;
}

int CurlHttpIO::debug_callback(CURL*, curl_infotype type, char* data, size_t size, void* debugdata)
{
    if (type == CURLINFO_TEXT && size)
    {
        data[size - 1] = 0;
        std::string errnoInfo;
        if (strstr(data, "SSL_ERROR_SYSCALL"))
        {
            // This function is called quite early by curl code, and hopefully no other call would have
            // modified errno in the meantime.
            errnoInfo = " (System errno: " + std::to_string(errno) +
#if defined(USE_OPENSSL)
                        "; OpenSSL last err: " + std::to_string(ERR_peek_last_error()) +
#endif
                        ")";
        }
        NET_verbose << (debugdata ? static_cast<HttpReq*>(debugdata)->getLogName() : string())
                    << "cURL: " << data << errnoInfo;
    }
    else if (type == CURLINFO_HEADER_IN && size)
    {
        NET_verbose << "CURL incoming header: " << std::string(data, size);
    }
    else if (type == CURLINFO_HEADER_OUT && size)
    {
        NET_verbose << "CURL outgoing header: " << std::string(data, size);
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
    httpctx->isIPv6 = false;
    httpctx->isCachedIp = false;
    httpctx->d = (req->type == REQ_JSON || req->method == METHOD_NONE) ? API : ((data ? len : req->out->size()) ? PUT : GET);
    req->httpiohandle = (void*)httpctx;

    bool validrequest = true;
    if ((proxyurl.size() && !proxyhost.size()) // malformed proxy string
        || (validrequest =
                crackurl(&req->posturl, &httpctx->scheme, &httpctx->hostname, &httpctx->port)) !=
               true) // invalid request
    {
        if (validrequest)
        {
            LOG_err << "Malformed proxy string: " << proxyurl;
        }
        else
        {
            LOG_err << "Invalid request: " << req->posturl;
        }

        delete httpctx;
        req->httpiohandle = NULL;
        req->status = REQ_FAILURE;
        statechange = true;
        return;
    }

    if (!ipv6requestsenabled && ipv6available() && Waiter::ds - ipv6deactivationtime > IPV6_RETRY_INTERVAL_DS)
    {
        ipv6requestsenabled = true;
    }

    // purge DNS cache if needed
    if (DNS_CACHE_EXPIRES && (Waiter::ds - lastdnspurge) > DNS_CACHE_TIMEOUT_DS)
    {
        std::map<string, CurlDNSEntry>::iterator it = dnscache.begin();

        while (it != dnscache.end())
        {
            CurlDNSEntry& entry = it->second;

            if (entry.ipv6.size() && entry.isIPv6Expired())
            {
                entry.ipv6timestamp = 0;
                entry.ipv6.clear();
            }

            if (entry.ipv4.size() && entry.isIPv4Expired())
            {
                entry.ipv4timestamp = 0;
                entry.ipv4.clear();
            }

            if (!entry.ipv6.size() && !entry.ipv4.size())
            {
                LOG_debug << "DNS cache record expired for " << it->first;
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
    req->postStartTime = std::chrono::steady_clock::now();

    if (proxyip.size() && req->method != METHOD_NONE)
    {
        // we are using a proxy, don't resolve the IP
        LOG_debug << "Sending the request through the proxy";
        send_request(httpctx);
        return;
    }

    if (proxyurl.size() && proxyinflight)
    {
        // we are waiting for a proxy, queue the request
        pendingrequests.push(httpctx);
        LOG_debug << "Queueing request for the proxy";
        return;
    }

    httpctx->hostheader = "Host: ";
    httpctx->hostheader.append(httpctx->hostname);

    CurlDNSEntry* dnsEntry = NULL;
    map<string, CurlDNSEntry>::iterator it = dnscache.find(httpctx->hostname);
    if (it != dnscache.end())
    {
        dnsEntry = &it->second;
    }

    if (ipv6requestsenabled)
    {
        if (dnsEntry && dnsEntry->ipv6.size() && !dnsEntry->isIPv6Expired())
        {
            NET_debug << "DNS cache hit for " << httpctx->hostname << " (IPv6) " << dnsEntry->ipv6;
            std::ostringstream oss;
            httpctx->isIPv6 = true;
            httpctx->isCachedIp = true;
            oss << "[" << dnsEntry->ipv6 << "]";
            httpctx->hostip = oss.str();
            send_request(httpctx);
            return;
        }
    }

    if (dnsEntry && dnsEntry->ipv4.size() && !dnsEntry->isIPv4Expired())
    {
        NET_debug << "DNS cache hit for " << httpctx->hostname << " (IPv4) " << dnsEntry->ipv4;
        httpctx->isIPv6 = false;
        httpctx->isCachedIp = true;
        httpctx->hostip = dnsEntry->ipv4;
        send_request(httpctx);
        return;
    }
    send_request(httpctx);
}

std::optional<Proxy> CurlHttpIO::getproxy() const
{
    // No prior proxy configuration.
    if (proxyurl.empty())
        return std::nullopt;

    Proxy proxy;

    // Copy proxy configuration.
    proxy.setCredentials(proxyusername, proxypassword);
    proxy.setProxyURL(proxyurl);
    proxy.setProxyType(proxytype);

    // Return (possibly invalid) proxy configuration.
    return proxy;
}

void CurlHttpIO::setproxy(const Proxy& proxy)
{
    // clear the previous proxy IP
    proxyip.clear();

    if (proxy.getProxyType() != Proxy::CUSTOM || !proxy.getProxyURL().size())
    {
        LOG_debug << "CurlHttpIO::setproxy: Invalid arguments. type: " << proxy.getProxyType()
                  << " url: " << proxy.getProxyURL() << " Invalidating inflight proxy changes";
        proxyscheme.clear();
        proxyhost.clear();
        proxyport = 0;
        proxyusername.clear();
        proxypassword.clear();
        proxytype = Proxy::NONE;
        proxyurl.clear();

        // send pending requests without a proxy
        send_pending_requests();
        return;
    }

    proxyurl = proxy.getProxyURL();
    proxyusername = proxy.getUsername();
    proxypassword = proxy.getPassword();
    proxytype = proxy.getProxyType();

    LOG_debug << "Setting proxy: " << proxyurl;

    if (!crackurl(&proxyurl, &proxyscheme, &proxyhost, &proxyport))
    {
        LOG_err << "Malformed proxy string: " << proxyurl;

        // invalidate inflight proxy changes

        // mark the proxy as invalid (proxyurl set but proxyhost not set)
        proxyhost.clear();
        proxyscheme.clear();

        // drop all pending requests
        drop_pending_requests();
        return;
    }

    ipv6requestsenabled = false;
    ipv6proxyenabled = ipv6requestsenabled;
    request_proxy_ip();
}

// cancel pending HTTP request
void CurlHttpIO::cancel(HttpReq* req)
{
    if (req->httpiohandle)
    {
        CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;
        if (httpctx->curl)
        {
            numconnections[httpctx->d]--;
            pausedrequests[httpctx->d].erase(httpctx->curl);
            curl_multi_remove_handle(curlm[httpctx->d], httpctx->curl);
            curl_easy_cleanup(httpctx->curl);
            curl_slist_free_all(httpctx->headers);
        }

        httpctx->req = NULL;

        if (req->status == REQ_FAILURE || httpctx->curl)
        {
            delete httpctx;
        }

        req->httpstatus = 0;
        req->mErrCode = 0;

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
    assert(handle);
    const CurlHttpContext* httpctx = static_cast<CurlHttpContext*>(handle);
    if (!httpctx || !httpctx->curl)
        return 0;

    curl_off_t bytes;
    if (const CURLcode errorCode = curl_easy_getinfo(httpctx->curl, CURLINFO_SIZE_UPLOAD_T, &bytes);
        errorCode)
    {
        LOG_err << "Unable to get CURLINFO_SIZE_UPLOAD_T. Error code: " << errorCode;
        return 0;
    }
    return bytes;
}

// process events
bool CurlHttpIO::doio()
{
    bool result;
    statechange = false;

    result = statechange;
    statechange = false;

    processcurlevents(API);
    result |= multidoio(curlm[API]);

    for (int d = GET; d == GET || d == PUT; d += PUT - GET)
    {
        partialdata[d] = 0;
        if (arerequestspaused[d])
        {
            arerequestspaused[d] = false;
            set<CURL *>::iterator it = pausedrequests[d].begin();
            while (!arerequestspaused[d] && it != pausedrequests[d].end())
            {
                CURL *easy_handle = *it;
                pausedrequests[d].erase(it++);
                curl_easy_pause(easy_handle, CURLPAUSE_CONT);
            }

            if (!arerequestspaused[d])
            {
                int dummy;
                curl_multi_socket_action(curlm[d], CURL_SOCKET_TIMEOUT, 0, &dummy);
            }
        }

        if (!arerequestspaused[d])
        {
            processcurlevents((direction_t)d);
            result |= multidoio(curlm[d]);
        }
    }

    return result;
}

bool CurlHttpIO::multidoio(CURLM *curlmhandle)
{
    int dummy = 0;
    CURLMsg* msg;
    bool result;

    while ((msg = curl_multi_info_read(curlmhandle, &dummy)) != nullptr)
    {
        HttpReq* req = NULL;
        if (curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (char**)&req) == CURLE_OK && req)
        {
            req->httpio = NULL;

            if (msg->msg == CURLMSG_DONE)
            {
                measureLatency(msg->easy_handle, req);

                CURLcode errorCode = msg->data.result;
                req->mErrCode = errorCode;
                if (errorCode != CURLE_OK && errorCode != CURLE_HTTP_RETURNED_ERROR && errorCode != CURLE_WRITE_ERROR)
                {
                    LOG_debug << req->getLogName() << "CURLMSG_DONE with error " << errorCode
                              << ": " << curl_easy_strerror(errorCode);

#if LIBCURL_VERSION_NUM >= 0x072c00 // At least cURL 7.44.0
                    if (errorCode == CURLE_SSL_PINNEDPUBKEYNOTMATCH)
                    {
                        pkpErrors++;
                        LOG_warn << req->getLogName() << "Invalid public key?";

                        if (pkpErrors == 3)
                        {
                            pkpErrors = 0;

                            LOG_err << req->getLogName()
                                    << "Invalid public key. Possible MITM attack!!";
                            req->sslcheckfailed = true;

                            struct curl_certinfo *ci;
                            if (curl_easy_getinfo(msg->easy_handle, CURLINFO_CERTINFO, &ci) == CURLE_OK)
                            {
                                LOG_warn << req->getLogName() << "Fake SSL certificate data:";
                                for (int i = 0; i < ci->num_of_certs; i++)
                                {
                                    struct curl_slist *slist = ci->certinfo[i];
                                    while (slist)
                                    {
                                        LOG_warn << req->getLogName() << i << ": " << slist->data;
                                        if (i == 0 && Utils::startswith(slist->data, "Issuer:"))
                                        {
                                            const char* issuer = strstr(slist->data, "CN = ");
                                            if (issuer)
                                            {
                                                issuer += 5;
                                            }
                                            else
                                            {
                                                issuer = strstr(slist->data, "CN=");
                                                if (issuer)
                                                {
                                                    issuer += 3;
                                                }
                                            }

                                            if (issuer)
                                            {
                                                req->sslfakeissuer = issuer;
                                            }
                                        }
                                        slist = slist->next;
                                    }
                                }

                                if (req->sslfakeissuer.size())
                                {
                                    LOG_debug << req->getLogName()
                                              << "Fake certificate issuer: " << req->sslfakeissuer;
                                }
                            }
                        }
                    }
                #endif
                }
                else if (req->protect)
                {
                    pkpErrors = 0;
                }

                long httpstatus;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &httpstatus);
                req->httpstatus = int(httpstatus);
                // Get the used ip address, if any.
                char* resolvedIpAddress = nullptr;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIMARY_IP, &resolvedIpAddress);

                LOG_debug << req->getLogName()
                          << "CURLMSG_DONE with HTTP status: " << req->httpstatus << " from "
                          << (req->httpiohandle ?
                                  (((CurlHttpContext*)req->httpiohandle)->hostname + " - " +
                                   (resolvedIpAddress ? resolvedIpAddress : "")) :
                                  "(unknown) ");
                if (req->httpstatus)
                {
                    if (req->mExpectRedirect && req->isRedirection()) // HTTP 3xx response
                    {
                        char *url = NULL;
                        curl_easy_getinfo(msg->easy_handle, CURLINFO_REDIRECT_URL, &url);
                        if (url)
                        {
                            req->mRedirectURL = url;
                            LOG_debug << req->getLogName() << "Redirected to " << req->mRedirectURL;
                        }
                    }

                    if (req->method == METHOD_NONE && req->httpiohandle)
                    {
                        char *ip = NULL;
                        CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;
                        if (curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIMARY_IP, &ip) == CURLE_OK
                              && ip && !strstr(httpctx->hostip.c_str(), ip))
                        {
                            LOG_err << req->getLogName() << "cURL has changed the original IP! "
                                    << httpctx->hostip << " -> " << ip;
                            req->in = strstr(ip, ":") ? (string("[") + ip + "]") : string(ip);
                        }
                        else
                        {
                            req->in = httpctx->hostip;
                        }
                        req->httpstatus = 200;
                    }

                    if (req->binary)
                    {
                        LOG_debug << req->getLogName() << "[received "
                                  << (req->buf ? req->bufpos : (int)req->in.size())
                                  << " bytes of raw data]";
                    }
                    else if (req->mChunked && static_cast<size_t>(req->bufpos) != req->in.size())
                    {
                        LOG_debug << req->getLogName() << "[received " << req->bufpos
                                  << " bytes of chunked data]";
                    }
                    else
                    {
                        if (gLogJSONRequests ||
                            req->in.size() < size_t(SimpleLogger::getMaxPayloadLogSize()))
                        {
                            LOG_debug << req->getLogName() << "Received " << req->in.size() << ": "
                                      << DirectMessage(req->in.c_str(), req->in.size())
                                      << " (at ds: " << Waiter::ds << ")";
                        }
                        else
                        {
                            LOG_debug
                                << req->getLogName() << "Received " << req->in.size() << ": "
                                << DirectMessage(req->in.c_str(),
                                                 static_cast<size_t>(
                                                     SimpleLogger::getMaxPayloadLogSize() / 2))
                                << " [...] "
                                << DirectMessage(req->in.c_str() + req->in.size() -
                                                     SimpleLogger::getMaxPayloadLogSize() / 2,
                                                 static_cast<size_t>(
                                                     SimpleLogger::getMaxPayloadLogSize() / 2));
                        }
                    }
                }

                // check httpstatus, redirecturl and response length
                m_off_t actualLength = req->buf != nullptr || req->mChunked ?
                                           req->bufpos :
                                           static_cast<m_off_t>(req->in.size());
                req->status =
                    ((req->httpstatus == 200 ||
                      (req->mExpectRedirect && req->isRedirection() && req->mRedirectURL.size())) &&
                     errorCode != CURLE_PARTIAL_FILE &&
                     (req->contentlength < 0 || req->contentlength == actualLength)) ?
                        REQ_SUCCESS :
                        REQ_FAILURE;

                if (req->status == REQ_SUCCESS)
                {
                    dnsok = true;
                    lastdata = Waiter::ds;
                    req->lastdata = Waiter::ds;
                }
                else
                {
                    LOG_warn << req->getLogName() << "REQ_FAILURE."
                             << " Status: " << req->httpstatus << " CURLcode: " << errorCode
                             << "  Content-Length: " << req->contentlength << "  buffer? "
                             << (req->buf != NULL) << "  bufferSize: " << actualLength;
                }

                if (req->httpstatus)
                {
                    success = true;
                }
            }
            else
            {
                req->status = REQ_FAILURE;
            }

            statechange = true;

            if (req->status == REQ_FAILURE && !req->httpstatus)
            {
                CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;
                if (httpctx)
                {
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

                    req->mDnsFailure = true;
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
                        if ((dnsEntry.ipv4.size() && !dnsEntry.isIPv4Expired()) ||
                            (!httpctx->isCachedIp))
                        {
                            numconnections[httpctx->d]--;
                            pausedrequests[httpctx->d].erase(msg->easy_handle);
                            curl_multi_remove_handle(curlmhandle, msg->easy_handle);
                            curl_easy_cleanup(msg->easy_handle);
                            curl_slist_free_all(httpctx->headers);
                            httpctx->isCachedIp = false;
                            httpctx->headers = NULL;
                            httpctx->curl = NULL;
                            req->httpio = this;
                            req->in.clear();
                            req->status = REQ_INFLIGHT;

                            if (dnsEntry.ipv4.size() && !dnsEntry.isIPv4Expired())
                            {
                                LOG_debug << req->getLogName() << "Retrying using IPv4 from cache";
                                httpctx->isIPv6 = false;
                                httpctx->hostip = dnsEntry.ipv4;
                                send_request(httpctx);
                            }
                            else
                            {
                                httpctx->hostip.clear();
                                LOG_debug << req->getLogName()
                                          << "Retrying with the pending DNS response";
                            }
                            return true;
                        }
                    }
                }
            }
        }
        else
        {
            req = NULL;
        }

        curl_multi_remove_handle(curlmhandle, msg->easy_handle);
        curl_easy_cleanup(msg->easy_handle);

        if (req)
        {
            inetstatus(req->httpstatus != 0);

            CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;
            if (httpctx)
            {
                numconnections[httpctx->d]--;
                pausedrequests[httpctx->d].erase(httpctx->curl);

                curl_slist_free_all(httpctx->headers);
                req->httpiohandle = NULL;

                httpctx->req = NULL;
                delete httpctx;
            }
        }
    }

    result = statechange;
    statechange = false;
    return result;
}

// Measure latency and connect time
void CurlHttpIO::measureLatency(CURL* easy_handle, HttpReq* req)
{
    if (auto httpReqXfer = dynamic_cast<HttpReqXfer*>(req))
    {
        double start_transfer_time = -1;
        double connect_time = -1;

        CURLcode start_transfer_time_res =
            curl_easy_getinfo(easy_handle, CURLINFO_STARTTRANSFER_TIME, &start_transfer_time);
        CURLcode connect_time_res =
            curl_easy_getinfo(easy_handle, CURLINFO_CONNECT_TIME, &connect_time);

        if (start_transfer_time_res == CURLE_OK)
        {
            start_transfer_time *= 1000; // Convert to milliseconds
            httpReqXfer->mStartTransferTime = start_transfer_time;
        }
        else
        {
            LOG_warn << "Failed to get start transfer time info: "
                     << curl_easy_strerror(start_transfer_time_res);
        }

        if (connect_time_res == CURLE_OK)
        {
            connect_time *= 1000; // Convert to milliseconds
            httpReqXfer->mConnectTime = connect_time;
        }
        else
        {
            LOG_warn << "Failed to get connect time info: " << curl_easy_strerror(connect_time_res);
        }

        LOG_verbose << "Connect time and start transfer latency for request " << req->getLogName()
                    << ": " << connect_time << " ms - " << start_transfer_time << " ms";
    }
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
            httpctx->req->httpiohandle = NULL;
            statechange = true;
        }

        httpctx->req = NULL;
        delete httpctx;
        pendingrequests.pop();
    }
}

size_t CurlHttpIO::read_data(void* ptr, size_t size, size_t nmemb, void* source)
{
    const char *buf;
    size_t totalsize;
    HttpReq *req = (HttpReq*)source;
    CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;
    size_t len = size * nmemb;
    CurlHttpIO* httpio = (CurlHttpIO*)req->httpio;

    if (httpctx->data)
    {
        buf = httpctx->data;
        totalsize = httpctx->len;
    }
    else
    {
        buf = req->out->data();
        totalsize = req->out->size();
    }

    buf += req->outpos;
    size_t nread = totalsize - req->outpos;
    if (nread > len)
    {
        nread = len;
    }

    if (!nread)
    {
        return 0;
    }

    req->lastdata = Waiter::ds;

    if (httpio->maxspeed[PUT])
    {
        bool isApi = (req->type == REQ_JSON);
        if (!isApi)
        {
            long maxbytes = long( ((httpio->maxspeed[PUT] - httpio->uploadSpeed) * SpeedController::SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS) - httpio->partialdata[PUT] );
            if (maxbytes <= 0)
            {
                httpio->pausedrequests[PUT].insert(httpctx->curl);
                httpio->arerequestspaused[PUT] = true;
                return CURL_READFUNC_PAUSE;
            }

            if (nread > (size_t)maxbytes)
            {
                nread = static_cast<size_t>(maxbytes);
            }
            httpio->partialdata[PUT] += nread;
        }
    }

    memcpy(ptr, buf, nread);
    req->outpos += nread;
    // LOG_debug << req->getLogName() << "Supplying " << nread << " bytes to cURL to send";
    return nread;
}

size_t CurlHttpIO::write_data(void* ptr, size_t size, size_t nmemb, void* target)
{
    int len = int(size * nmemb);
    HttpReq *req = (HttpReq*)target;
    CurlHttpIO* httpio = (CurlHttpIO*)req->httpio;
    if (httpio)
    {
        if (httpio->maxspeed[GET])
        {
            CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;
            bool isUpload = (httpctx->data ? httpctx->len : req->out->size()) > 0;
            bool isApi = (req->type == REQ_JSON);
            if (!isApi && !isUpload)
            {
                if ((httpio->downloadSpeed + ((httpio->partialdata[GET] + len) / static_cast<m_off_t>(SpeedController::SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS))) > httpio->maxspeed[GET])
                {
                    httpio->pausedrequests[GET].insert(httpctx->curl);
                    httpio->arerequestspaused[GET] = true;
                    return CURL_WRITEFUNC_PAUSE;
                }
                httpio->partialdata[GET] += len;
            }
        }

        if (len)
        {
            req->put(ptr, static_cast<unsigned>(len), true);
        }

        httpio->lastdata = Waiter::ds;
        req->lastdata = Waiter::ds;
    }

    return static_cast<size_t>(len);
}

// set contentlength according to Original-Content-Length header
size_t CurlHttpIO::check_header(const char* ptr, size_t size, size_t nmemb, void* target)
{
    HttpReq *req = (HttpReq*)target;
    size_t len = size * nmemb;
    if (len > 2)
    {
        NET_verbose << req->getLogName() << "Header: " << string(ptr, len - 2);
    }
    assert(Utils::endswith(ptr, len, "\r\n", 2));
    const char* val = nullptr;
    if (Utils::startswith(ptr, "HTTP/"))
    {
        if (req->contentlength >= 0)
        {
            // For authentication with some proxies, cURL sends two requests in the context of a single one
            // Content-Length is reset here to not take into account the header from the first response

            LOG_warn << req->getLogName()
                     << "Receiving a second response. Resetting Content-Length";
            req->contentlength = -1;
        }

        return size * nmemb;
    }
    else if ((val = Utils::startswith(ptr, "Content-Length:")) != nullptr)
    {
        if (req->contentlength < 0)
        {
            req->setcontentlength(atoll(val));
        }
    }
    else if ((val = Utils::startswith(ptr, "Original-Content-Length:")) != nullptr)
    {
        req->setcontentlength(atoll(val));
    }
    else if ((val = Utils::startswith(ptr, "X-MEGA-Time-Left:")) != nullptr)
    {
        req->timeleft = atol(val);
    }
    else if ((val = Utils::startswith(ptr, "Content-Type:")) != nullptr)
    {
        req->contenttype.assign(val, len - 15); // length of "Content-Type:" + 2
    }
    else if ((val = Utils::startswith(ptr, "X-Hashcash:")) != nullptr)
    {
        const char* end = ptr + len - 3; // point to the char before CRLF terminator
        if (end - val < 4) // minimum hashcash len is 5
        {
            LOG_warn << req->getLogName() << "Ignoring too short X-Hashcash header";
            return len;
        }
        // trim trailing CRLF, from right to left, up to end of "X-Hashcash:"
        while (end > val && *end < ' ')
            end--;
        assert(end - val >= 0);
        string buffer{val, static_cast<size_t>((end - val) + 1)};
        LOG_warn << req->getLogName() << "X-Hashcash received:" << buffer;

        // Example of hashcash header
        // 1:100:1731410499:RUvIePV2PNO8ofg8xp1aT5ugBcKSEzwKoLBw9o4E6F_fmn44eC3oMpv388UtFl2K
        // <version>:<easiness>:<timestamp>:<b64token>

        std::stringstream ss(buffer);
        vector<string> hc;
        for (size_t i = 0; i < 4; i++)
        {
            string buf;
            if (!getline(ss, buf, ':'))
                break;
            hc.push_back(std::move(buf));
        }
        if (hc.size() != 4 // incomplete data
            || stoi(hc[0]) != 1 // header version
            || stoi(hc[1]) < 0 || stoi(hc[1]) > 255 // invalid easiness [0, 255]
            || hc[3].size() != 64)  // token is 64 chars in B64
        {
            req->mHashcashToken.clear();
        }
        else
        {
            req->mHashcashToken = hc[3].substr(0, 64);
            req->mHashcashEasiness = static_cast<uint8_t>(stoi(hc[1]));
        }
    }
    else
    {
        return len;
    }

    if (req->httpio)
    {
        req->httpio->lastdata = Waiter::ds;
        req->lastdata = Waiter::ds;
    }

    return len;
}

int CurlHttpIO::seek_data(void *userp, curl_off_t offset, int origin)
{
    HttpReq *req = (HttpReq*)userp;
    CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;
    curl_off_t newoffset;
    size_t totalsize;

    if (httpctx->data)
    {
        totalsize = httpctx->len;
    }
    else
    {
        totalsize = req->out->size();
    }

    switch (origin)
    {
    case SEEK_SET:
        newoffset = offset;
        break;
    case SEEK_CUR:
        newoffset = static_cast<curl_off_t>(req->outpos) + offset;
        break;
    case SEEK_END:
        newoffset = static_cast<curl_off_t>(totalsize) + offset;
        break;
    default:
        LOG_err << "Invalid origin in seek function: " << origin;
        return CURL_SEEKFUNC_FAIL;
    }

    if (newoffset > (int) totalsize || newoffset < 0)
    {
        LOG_err << "Invalid offset " << origin << " " << offset << " " << totalsize
                << " " << req->outbuf << " " << newoffset;
        return CURL_SEEKFUNC_FAIL;
    }
    req->outpos = size_t(newoffset);
    LOG_debug << "Successful seek to position " << newoffset << " of " << totalsize;
    return CURL_SEEKFUNC_OK;
}

int CurlHttpIO::socket_callback(CURL *, curl_socket_t s, int what, void *userp, void *, direction_t d)
{
    CurlHttpIO *httpio = (CurlHttpIO *)userp;
    SockInfoMap &socketmap = httpio->curlsockets[d];

    if (what == CURL_POLL_REMOVE)
    {
        auto it = socketmap.find(s);
        if (it != socketmap.end())
        {
            LOG_debug << "Removing socket " << s;

#if defined(_WIN32)
            it->second.closeEvent();
#endif
            it->second.mode = 0;
        }
    }
    else
    {
        auto it = socketmap.find(s);
        if (it == socketmap.end())
        {
            LOG_debug << "Adding curl socket " << s << " to " << what;
#ifdef WIN32
            auto pair = socketmap.emplace(s, SockInfo(httpio->mSocketsWaitEvent));
#else
            auto pair = socketmap.emplace(s, SockInfo());
#endif
            it = pair.first;
        }
        else
        {
            // Networking seems to be fine after performance improvments, no need for this logging anymore - but keep it in comments for a while to inform people debugging older logs
            //LOG_debug << "Setting curl socket " << s << " to " << what;
        }

        auto& info = it->second;
        info.fd = s;
        info.mode = what;
#if defined(_WIN32)
        info.createAssociateEvent();

        if (what & CURL_POLL_OUT)
        {
            info.signalledWrite = true;
        }
#endif
    }

    return 0;
}

// CURL doco: When set, this callback function gets called by libcurl when the socket has been
// created, but before the connect call to allow applications to change specific socket options.The
// callback's purpose argument identifies the exact purpose for this particular socket:
int CurlHttpIO::sockopt_callback([[maybe_unused]] void* clientp, curl_socket_t, curlsocktype)
{
    return CURL_SOCKOPT_OK;
}

int CurlHttpIO::api_socket_callback(CURL *e, curl_socket_t s, int what, void *userp, void *socketp)
{
    return socket_callback(e, s, what, userp, socketp, API);
}

int CurlHttpIO::download_socket_callback(CURL *e, curl_socket_t s, int what, void *userp, void *socketp)
{
    return socket_callback(e, s, what, userp, socketp, GET);
}

int CurlHttpIO::upload_socket_callback(CURL *e, curl_socket_t s, int what, void *userp, void *socketp)
{
    return socket_callback(e, s, what, userp, socketp, PUT);
}

int CurlHttpIO::timer_callback(CURLM *, long timeout_ms, void *userp, direction_t d)
{
    CurlHttpIO *httpio = (CurlHttpIO *)userp;
    //auto oldValue = httpio->curltimeoutreset[d];
    if (timeout_ms < 0)
    {
        httpio->curltimeoutreset[d] = -1;
    }
    else
    {
        m_time_t timeoutds = timeout_ms / 100;
        if (timeout_ms % 100)
        {
            timeoutds++;
        }

        httpio->curltimeoutreset[d] = Waiter::ds + timeoutds;
    }

    // Networking seems to be fine after performance improvments, no need for this logging anymore - but keep it in comments for a while to inform people debugging older logs
    //if (oldValue != httpio->curltimeoutreset[d])
    //{
    //    LOG_debug << "Set cURL timeout[" << d << "] to " << httpio->curltimeoutreset[d] << " from " << timeout_ms << "(ms) at ds: " << Waiter::ds;
    //}
    return 0;
}

int CurlHttpIO::api_timer_callback(CURLM *multi, long timeout_ms, void *userp)
{
    return timer_callback(multi, timeout_ms, userp, API);
}

int CurlHttpIO::download_timer_callback(CURLM *multi, long timeout_ms, void *userp)
{
    return timer_callback(multi, timeout_ms, userp, GET);
}

int CurlHttpIO::upload_timer_callback(CURLM *multi, long timeout_ms, void *userp)
{
    return timer_callback(multi, timeout_ms, userp, PUT);
}

#ifdef USE_OPENSSL
CURLcode CurlHttpIO::ssl_ctx_function(CURL*, void* sslctx, void*req)
{
    SSL_CTX_set_cert_verify_callback((SSL_CTX*)sslctx, cert_verify_callback, req);
    return CURLE_OK;
}

#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || defined (LIBRESSL_VERSION_NUMBER)
   #define X509_STORE_CTX_get0_cert(ctx) (ctx->cert)
   #define X509_STORE_CTX_get0_untrusted(ctx) (ctx->untrusted)
   #define EVP_PKEY_get0_DSA(_pkey_) ((_pkey_)->pkey.dsa)
   #define EVP_PKEY_get0_RSA(_pkey_) ((_pkey_)->pkey.rsa)
#endif

#if (OPENSSL_VERSION_NUMBER < 0x1010100fL) || defined (LIBRESSL_VERSION_NUMBER)
const BIGNUM *RSA_get0_n(const RSA *rsa)
{
#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || defined (LIBRESSL_VERSION_NUMBER)
    return rsa->n;
#else
    const BIGNUM *result;
    RSA_get0_key(rsa, &result, NULL, NULL);
    return result;
#endif
}

const BIGNUM *RSA_get0_e(const RSA *rsa)
{
#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || defined (LIBRESSL_VERSION_NUMBER)
    return rsa->e;
#else
    const BIGNUM *result;
    RSA_get0_key(rsa, NULL, &result, NULL);
    return result;
#endif
}

const BIGNUM *RSA_get0_d(const RSA *rsa)
{
#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || defined (LIBRESSL_VERSION_NUMBER)
    return rsa->d;
#else
    const BIGNUM *result;
    RSA_get0_key(rsa, NULL, NULL, &result);
    return result;
#endif
}
#endif

// SSL public key pinning
int CurlHttpIO::cert_verify_callback(X509_STORE_CTX* ctx, void* req)
{
    HttpReq *request = (HttpReq *)req;
    CurlHttpIO *httpio = (CurlHttpIO *)request->httpio;
    unsigned char buf[sizeof(APISSLMODULUS1) - 1];
    int ok = 0;

    if (httpio->disablepkp)
    {
        LOG_warn << "Public key pinning disabled.";
        return 1;
    }

    EVP_PKEY* evp = X509_PUBKEY_get(X509_get_X509_PUBKEY(X509_STORE_CTX_get0_cert(ctx)));
    if (evp && EVP_PKEY_id(evp) == EVP_PKEY_RSA)
    {
        // get needed components of RSA key:
        // n: modulus common to both public and private key;
        // e: public exponent.
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        const rsa_st* rsaKey = EVP_PKEY_get0_RSA(evp);
        const BIGNUM* rsaN = RSA_get0_n(rsaKey);
        const BIGNUM* rsaE = RSA_get0_e(rsaKey);
        bool rsaOk = true;
#else
        BIGNUM* rsaN = nullptr;
        BIGNUM* rsaE = nullptr;
        bool rsaOk = EVP_PKEY_get_bn_param(evp, "n", &rsaN) &&
                     EVP_PKEY_get_bn_param(evp, "e", &rsaE);

        // ensure cleanup
        std::unique_ptr<BIGNUM, decltype(&BN_free)> nCleanup(rsaN, &BN_free);
        std::unique_ptr<BIGNUM, decltype(&BN_free)> eCleanup(rsaE, &BN_free);
#endif

        if (rsaOk &&
            BN_num_bytes(rsaN) == sizeof APISSLMODULUS1 - 1 &&
            BN_num_bytes(rsaE) == sizeof APISSLEXPONENT - 1)
        {
            BN_bn2bin(rsaN, buf);

            // check the public key matches for the URL of the connection (API or SFU-stats)
            if ((Utils::startswith(request->posturl, httpio->APIURL) &&
                 (!memcmp(buf, APISSLMODULUS1, sizeof APISSLMODULUS1 - 1) ||
                  !memcmp(buf, APISSLMODULUS2, sizeof APISSLMODULUS2 - 1))) ||
                (Utils::startswith(request->posturl, MegaClient::SFUSTATSURL) &&
                 (!memcmp(buf, SFUSTATSSSLMODULUS, sizeof SFUSTATSSSLMODULUS - 1) ||
                  !memcmp(buf, SFUSTATSSSLMODULUS2, sizeof SFUSTATSSSLMODULUS2 - 1))))
            {
                BN_bn2bin(rsaE, buf);

                if (!memcmp(buf, APISSLEXPONENT, sizeof APISSLEXPONENT - 1))
                {
                    LOG_debug << "SSL public key OK";
                    ok = 1;
                }
            }
            else
            {
                LOG_warn << "Public key mismatch for " << request->posturl;
            }
        }
        else
        {
            LOG_warn << "Public key size mismatch " << BN_num_bytes(rsaN) << " " << BN_num_bytes(rsaE);
        }
    }
    else
    {
        LOG_warn << "Public key not found";
    }

    EVP_PKEY_free(evp);

    if (!ok)
    {
        httpio->pkpErrors++;
        LOG_warn << "Invalid public key?";

        if (httpio->pkpErrors == 3)
        {
            httpio->pkpErrors = 0;

            LOG_err << "Invalid public key. Possible MITM attack!!";
            request->sslcheckfailed = true;
            request->sslfakeissuer.resize(256);
            int len = X509_NAME_get_text_by_NID (X509_get_issuer_name (X509_STORE_CTX_get0_cert(ctx)),
                                                 NID_commonName,
                                                 (char *)request->sslfakeissuer.data(),
                                                 int(request->sslfakeissuer.size()));
            request->sslfakeissuer.resize(len > 0 ? static_cast<size_t>(len) : 0);
            LOG_debug << "Fake certificate issuer: " << request->sslfakeissuer;
        }
    }

    return ok;
}
#endif

CurlDNSEntry::CurlDNSEntry()
{
    ipv4timestamp = 0;
    ipv6timestamp = 0;
}

bool CurlDNSEntry::isIPv4Expired()
{
    return (DNS_CACHE_EXPIRES && (Waiter::ds - ipv4timestamp) >= DNS_CACHE_TIMEOUT_DS);
}

bool CurlDNSEntry::isIPv6Expired()
{
    return (DNS_CACHE_EXPIRES && (Waiter::ds - ipv6timestamp) >= DNS_CACHE_TIMEOUT_DS);
}

void CurlHttpIO::addDnsResolution(
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>& dnsList,
    const string& host,
    const string& ip,
    const int port)
{
    string curlListEntry = "+" + host + ":" + std::to_string(port) + ":" + ip;
    dnsList.reset(curl_slist_append(dnsList.release(), curlListEntry.c_str()));
}

} // namespace
