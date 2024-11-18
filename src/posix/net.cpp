/**
 * @file posix/net.cpp
 * @brief POSIX network access layer (using cURL + c-ares)
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
#include "mega/posix/meganet.h"
#include "mega/logging.h"

#if defined(USE_OPENSSL)
#include <openssl/err.h>
#endif

#if defined(__ANDROID__) && ARES_VERSION >= 0x010F00
#include <jni.h>
extern JavaVM *MEGAjvm;
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
        if (!(mutex = sslMutexes[lockNumber]))
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
void CurlHttpIO::id_function(CRYPTO_THREADID* id)
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
        LOG_debug << "cURL version: " << data->version;
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

        if (data->version_num < 0x072c00 // At least cURL 7.44.0
        #ifdef USE_OPENSSL
                && !(strstr(curlssl.c_str(), "openssl") && data->version_num > 0x070b00)
                // or cURL 7.11.0 with OpenSSL
        #endif
            )
        {
            LOG_fatal << "cURL built without public key pinning support. Aborting.";
            throw std::runtime_error("cURL built without public key pinning support. Aborting.");
        }
    }

    if (data->libz_version)
    {
        LOG_debug << "libz version: " << data->libz_version;
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
        throw std::runtime_error("cURL built without HTTP/HTTPS support. Aborting.");
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
        int numLocks = CRYPTO_num_locks();
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
#ifdef MEGA_USE_C_ARES
        ares_library_init(ARES_LIB_INIT_ALL);

        const char *aresversion = ares_version(NULL);
        if (aresversion)
        {
            LOG_debug << "c-ares version: " << aresversion;
        }

#if (defined(ANDROID) || defined(__ANDROID__)) && ARES_VERSION >= 0x010F00
        initialize_android();
#endif
#endif
    };

    curlMutex.unlock();

    curlm[API] = curl_multi_init();
    curlm[GET] = curl_multi_init();
    curlm[PUT] = curl_multi_init();
    numconnections[API] = 0;
    numconnections[GET] = 0;
    numconnections[PUT] = 0;

#ifdef MEGA_USE_C_ARES
    struct ares_options options;
    options.tries = 2;
    ares_init_options(&ares, &options, ARES_OPT_TRIES);
    arestimeout = -1;
#endif

#ifdef MEGA_USE_C_ARES
    filterDNSservers();
#endif

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

#ifdef MEGA_USE_C_ARES
void CurlHttpIO::filterDNSservers()
{
    string newservers;
    string serverlist;
    set<string> serverset;
    vector<string> filteredservers;

    ares_addr_node *servers;
    ares_addr_node *server;
    if (ares_get_servers(ares, &servers) == ARES_SUCCESS)
    {
        bool first = true;
        bool filtered = false;
        server = servers;
        while (server)
        {
            char straddr[INET6_ADDRSTRLEN];
            straddr[0] = 0;

            if (server->family == AF_INET6)
            {
                mega_inet_ntop(PF_INET6, &server->addr, straddr, sizeof(straddr));
            }
            else if (server->family == AF_INET)
            {
                mega_inet_ntop(PF_INET, &server->addr, straddr, sizeof(straddr));
            }
            else
            {
                LOG_warn << "Unknown IP address family: " << server->family;
            }

            if (straddr[0])
            {
                serverlist.append(straddr);
                serverlist.append(",");
            }

            if (straddr[0]
                    && serverset.find(straddr) == serverset.end()
                    && strncasecmp(straddr, "fec0:", 5)
                    && strncasecmp(straddr, "169.254.", 8))
            {
                if (!first)
                {
                    newservers.append(",");
                }

                newservers.append(straddr);
                serverset.insert(straddr);
                first = false;
            }
            else
            {
                filtered = true;
                if (!straddr[0])
                {
                    LOG_debug << "Filtering unkwnown address of DNS server";
                }
                else if (serverset.find(straddr) == serverset.end())
                {
                    serverset.insert(straddr);
                    filteredservers.push_back(straddr);
                }
            }

            server = server->next;
        }

        if (serverlist.size())
        {
            serverlist.resize(serverlist.size() - 1);
        }
        LOG_debug << "DNS servers: " << serverlist;

        if (filtered && (newservers.size() || filteredservers.size()))
        {
            for (unsigned int i = 0; i < filteredservers.size(); i++)
            {
                if (newservers.size())
                {
                    newservers.append(",");
                }

                newservers.append(filteredservers[i]);
            }

            LOG_debug << "Setting filtered DNS servers: " << newservers;
            ares_set_servers_csv(ares, newservers.c_str());
        }

        ares_free_data(servers);
    }
}

void CurlHttpIO::addaresevents(Waiter *waiter)
{
#ifdef MEGA_MEASURE_CODE
    CodeCounter::ScopeTimer ccst(countAddAresEventsCode);
#endif

    SockInfoMap prevAressockets;   // if there are SockInfo records that were in use, and won't be anymore, they will be deleted with this
    prevAressockets.swap(aressockets);

    ares_socket_t socks[ARES_GETSOCK_MAXNUM];
    int bitmask = ares_getsock(ares, socks, ARES_GETSOCK_MAXNUM);
    for (int i = 0; i < ARES_GETSOCK_MAXNUM; i++)
    {
        bool readable = ARES_GETSOCK_READABLE(bitmask, i);
        bool writeable = ARES_GETSOCK_WRITABLE(bitmask, i);

        if (readable || writeable)
        {
            // take the old record from the prior version of the map, if there is one, and then we will update it
            auto it = prevAressockets.find(socks[i]);
            if (it == prevAressockets.end())
            {
#ifdef WIN32
                auto pair = aressockets.emplace(socks[i], SockInfo(mSocketsWaitEvent));
#else
                auto pair = aressockets.emplace(socks[i], SockInfo());
#endif
                it = pair.first;
            }
            else
            {
                auto pair = aressockets.emplace(socks[i], std::move(it->second));
                prevAressockets.erase(it);
                it = pair.first;
            }
            SockInfo& info = it->second;
            info.mode = 0;

            if (readable)
            {
                info.fd = socks[i];
                info.mode |= SockInfo::READ;
            }

            if (writeable)
            {
                info.fd = socks[i];
                info.mode |= SockInfo::WRITE;
            }

#if defined(_WIN32)
            info.createAssociateEvent();
#else
            if (readable)
            {
                MEGA_FD_SET(info.fd, &((PosixWaiter *)waiter)->rfds);
                ((PosixWaiter *)waiter)->bumpmaxfd(info.fd);
            }
            if (writeable)
            {
                MEGA_FD_SET(info.fd, &((PosixWaiter *)waiter)->wfds);
                ((PosixWaiter *)waiter)->bumpmaxfd(info.fd);
            }
#endif
        }
    }

#if defined(_WIN32)
    for (auto& mapPair : prevAressockets)
    {
        // We pass false for c-ares becase we can't be sure if c-ares closed the socket or not
        // If it's not using the socket, the event should not be triggered, and even if it is
        // then we just do one extra loop.
        mapPair.second.closeEvent(false);
    }
#endif
}

#endif // #ifdef MEGA_USE_C_ARES

void CurlHttpIO::addcurlevents(Waiter *waiter, direction_t d)
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
            MEGA_FD_SET(info.fd, &((PosixWaiter *)waiter)->rfds);
            ((PosixWaiter *)waiter)->bumpmaxfd(info.fd);
        }

        if (info.mode & SockInfo::WRITE)
        {
            MEGA_FD_SET(info.fd, &((PosixWaiter *)waiter)->wfds);
            ((PosixWaiter *)waiter)->bumpmaxfd(info.fd);
        }
#endif
   }

#if defined(_WIN32)
    if (anyWriters)
    {
        // so long as we are writing at least one socket, keep looping until the socket is full, then start waiting on its associated event
        static_cast<WinWaiter*>(waiter)->maxds = 0;
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

#ifdef MEGA_USE_C_ARES
void CurlHttpIO::closearesevents()
{
#if defined(_WIN32)
    for (auto& mapPair : aressockets)
    {
        mapPair.second.closeEvent(false);
    }
#endif
    aressockets.clear();
}
#endif

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

#ifdef MEGA_USE_C_ARES
void CurlHttpIO::processaresevents()
{
#ifdef MEGA_MEASURE_CODE
    CodeCounter::ScopeTimer ccst(countProcessAresEventsCode);
#endif

#ifndef _WIN32
    auto *rfds = &((PosixWaiter *)waiter)->rfds;
    auto *wfds = &((PosixWaiter *)waiter)->wfds;
#endif

    for (auto& mapPair : aressockets)
    {
        SockInfo &info = mapPair.second;
        if (!info.mode)
        {
            continue;
        }

#if defined(_WIN32)
        bool read, write;
        if (info.checkEvent(read, write, false))  // if checkEvent returns true, both `read` and `write` have been set.
        {
            ares_process_fd(ares, read ? info.fd : ARES_SOCKET_BAD, write ? info.fd : ARES_SOCKET_BAD);
        }
#else
        if (((info.mode & SockInfo::READ) && MEGA_FD_ISSET(info.fd, rfds)) || ((info.mode & SockInfo::WRITE) && MEGA_FD_ISSET(info.fd, wfds)))
        {
            ares_process_fd(ares,
                            ((info.mode & SockInfo::READ) && MEGA_FD_ISSET(info.fd, rfds)) ? info.fd : ARES_SOCKET_BAD,
                            ((info.mode & SockInfo::WRITE) && MEGA_FD_ISSET(info.fd, wfds)) ? info.fd : ARES_SOCKET_BAD);
        }
#endif
    }

    if (arestimeout >= 0 && arestimeout <= Waiter::ds)
    {
        arestimeout = -1;
        ares_process_fd(ares, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
    }
}
#endif

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
#ifdef MEGA_USE_C_ARES
    ares_destroy(ares);
#endif
    curl_multi_cleanup(curlm[API]);
    curl_multi_cleanup(curlm[GET]);
    curl_multi_cleanup(curlm[PUT]);
    curl_share_cleanup(curlsh);

#ifdef MEGA_USE_C_ARES
    closearesevents();
#endif
    closecurlevents(API);
    closecurlevents(GET);
    closecurlevents(PUT);

#ifdef WIN32
    WSACloseEvent(mSocketsWaitEvent);
#endif

    curlMutex.lock();
    if (--instanceCount == 0)
    {
#ifdef MEGA_USE_C_ARES
        ares_library_cleanup();
#endif
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

#ifdef MEGA_USE_C_ARES
void CurlHttpIO::setdnsservers(const char* servers)
{
    if (servers)
    {
        lastdnspurge = Waiter::ds + DNS_CACHE_TIMEOUT_DS / 2;
        if (DNS_CACHE_EXPIRES)
        {
            dnscache.clear();
        }

        dnsservers = servers;

        LOG_debug << "Using custom DNS servers: " << dnsservers;
        ares_set_servers_csv(ares, servers);
    }
}
#endif

void CurlHttpIO::disconnect()
{
    LOG_debug << "Reinitializing the network layer";
    disconnecting = true;
    assert(!numconnections[API] && !numconnections[GET] && !numconnections[PUT]);

#ifdef MEGA_USE_C_ARES
    ares_destroy(ares);
#endif
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

#ifdef MEGA_USE_C_ARES
    closearesevents();
#endif
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
#ifdef MEGA_USE_C_ARES
    struct ares_options options;
    options.tries = 2;
    ares_init_options(&ares, &options, ARES_OPT_TRIES);
    arestimeout = -1;
#endif

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
#ifdef MEGA_USE_C_ARES
    if (dnsservers.size())
    {
        LOG_debug << "Using custom DNS servers: " << dnsservers;
        ares_set_servers_csv(ares, dnsservers.c_str());
    }
    else
    {
        filterDNSservers();
    }
#endif

    if (proxyurl.size() && !proxyip.size())
    {
        LOG_debug << "Unresolved proxy name. Resolving...";
        request_proxy_ip();
    }
}

bool CurlHttpIO::setmaxdownloadspeed(m_off_t bpslimit)
{
    maxspeed[GET] = bpslimit;
    return true;
}

bool CurlHttpIO::setmaxuploadspeed(m_off_t bpslimit)
{
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
        int dummyport;
        const string& url = urls[i]; // this is "free" and helps with debugging

        crackurl(&url, &dummyscheme, &host, &dummyport);

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

#ifdef MEGA_USE_C_ARES
    addaresevents(waiter);
#endif
    addcurlevents(waiter, API);

#ifdef WIN32
    ((WinWaiter *)waiter)->addhandle(mSocketsWaitEvent, Waiter::NEEDEXEC);
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

    if ((curltimeoutms < 0 || curltimeoutms > MAX_SPEED_CONTROL_TIMEOUT_MS)
            && (downloadSpeed || uploadSpeed))
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
#ifdef MEGA_USE_C_ARES
    timeval tv;
    if (ares_timeout(ares, NULL, &tv))
    {
        arestimeout = tv.tv_sec * 10 + tv.tv_usec / 100000;
        if (!arestimeout && tv.tv_usec)
        {
            arestimeout = 1;
        }

        if (arestimeout < waiter->maxds)
        {
            waiter->maxds = dstime(arestimeout);
        }
        arestimeout += Waiter::ds;
    }
    else
    {
        arestimeout = -1;
    }
#endif
}

#ifdef MEGA_USE_C_ARES
void CurlHttpIO::proxy_ready_callback(void* arg, int status, int, hostent* host)
{
    // the name of a proxy has been resolved
    CurlHttpContext* httpctx = (CurlHttpContext*)arg;
    CurlHttpIO* httpio = httpctx->httpio;

    LOG_debug << "c-ares info received (proxy)";

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
            LOG_debug << "Proxy ready";

            // name resolution finished.
            // nothing more to do.
            // free resources and continue sending requests.
            delete httpctx;
            httpio->send_pending_requests();
        }
        else
        {
            LOG_debug << "Proxy ready. Waiting for c-ares";
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
        LOG_debug << "Received a valid IP for the proxy";

        // save the IP of the proxy
        char ip[INET6_ADDRSTRLEN];

        mega_inet_ntop(host->h_addrtype, host->h_addr_list[0], ip, sizeof ip);
        httpctx->hostip = ip;
        httpctx->isIPv6 = host->h_addrtype == PF_INET6;

        if (httpctx->isIPv6 && ip[0] != '[')
        {
            httpctx->hostip.insert(0, "[");
            httpctx->hostip.append("]");
        }
    }
    else if (status != ARES_SUCCESS)
    {
        LOG_warn << "c-ares error (proxy) " << status;
    }

    if (!httpctx->ares_pending)
    {
        LOG_debug << "c-ares request finished (proxy)";

        // name resolution finished
        // if the IP is valid, use it and continue sending requests.
        if (httpio->proxyhost == httpctx->hostname && httpctx->hostip.size())
        {
            std::ostringstream oss;

            oss << httpctx->hostip << ":" << httpio->proxyport;
            httpio->proxyip = oss.str();

            LOG_info << "Updated proxy URL: " << httpio->proxyip;

            httpio->inetstatus(true);

            httpio->send_pending_requests();
        }
        else if (!httpio->proxyinflight)
        {
            LOG_err << "Invalid proxy IP";

            httpio->inetstatus(false);

            // the IP isn't up to date and there aren't pending
            // name resolutions for proxies. Abort requests.
            httpio->drop_pending_requests();

            if (status != ARES_EDESTRUCTION)
            {
                // reinitialize c-ares to prevent persistent hangs
                httpio->reset = true;
            }
        }
        else
        {
            LOG_debug << "Waiting for the IP of the proxy";
        }

        // nothing more to do - free resources
        delete httpctx;
    }
    else
    {
        LOG_debug << "Waiting for the completion of the c-ares request (proxy)";
    }
}

void CurlHttpIO::ares_completed_callback(void* arg, int status, int, struct hostent* host)
{
    CurlHttpContext* httpctx = (CurlHttpContext*)arg;
    CurlHttpIO* httpio = httpctx->httpio;
    HttpReq* req = httpctx->req;
    bool invalidcache = false;
    httpctx->ares_pending--;

    LOG_debug << "c-ares info received";

    // check if result is valid
    if (status == ARES_SUCCESS && host && host->h_addr_list[0])
    {
        char ip[INET6_ADDRSTRLEN];
        mega_inet_ntop(host->h_addrtype, host->h_addr_list[0], ip, sizeof(ip));

        LOG_debug << "Received a valid IP for "<< httpctx->hostname << ": " << ip;

        httpio->inetstatus(true);

        // add to DNS cache
        CurlDNSEntry& dnsEntry = httpio->dnscache[httpctx->hostname];

        int i = 0;
        bool incache = false;
        if ((host->h_addrtype == PF_INET6 && dnsEntry.ipv6.size())
                || (host->h_addrtype != PF_INET6 && dnsEntry.ipv4.size()))
        {
            invalidcache = true;
            while (host->h_addr_list[i] != NULL)
            {
                char checkip[INET6_ADDRSTRLEN];
                mega_inet_ntop(host->h_addrtype, host->h_addr_list[i], checkip, sizeof(checkip));
                if (host->h_addrtype == PF_INET6)
                {
                    if (!strcmp(dnsEntry.ipv6.c_str(), checkip))
                    {
                        incache = true;
                        invalidcache = false;
                        break;
                    }
                }
                else
                {
                    if (!strcmp(dnsEntry.ipv4.c_str(), checkip))
                    {
                        incache = true;
                        invalidcache = false;
                        break;
                    }
                }
                i++;
            }
        }

        if (incache)
        {
            LOG_debug << "The current DNS cache record is still valid";
        }
        else if (invalidcache)
        {
            LOG_warn << "The current DNS cache record is invalid";
        }

        if (host->h_addrtype == PF_INET6)
        {
            if (!incache)
            {
                dnsEntry.ipv6 = ip;
            }
            dnsEntry.ipv6timestamp = Waiter::ds;
        }
        else
        {
            if (!incache)
            {
                dnsEntry.ipv4 = ip;
            }
            dnsEntry.ipv4timestamp = Waiter::ds;
        }

        // IPv6 takes precedence over IPv4
        if (!httpctx->hostip.size() || (host->h_addrtype == PF_INET6 && !httpctx->curl))
        {
            httpctx->isIPv6 = host->h_addrtype == PF_INET6;

            //save the IP for this request
            std::ostringstream oss;
            if (httpctx->isIPv6)
            {
                oss << "[" << ip << "]";
            }
            else
            {
                oss << ip;
            }

            httpctx->hostip = oss.str();
        }
    }
    else if (status != ARES_SUCCESS)
    {
        LOG_warn << "c-ares error. code: " << status;
    }
    else
    {
        LOG_err << "Unknown c-ares error";
    }

    if (!req) // the request was cancelled
    {
        if (!httpctx->ares_pending)
        {
            LOG_debug << "Request cancelled";
            delete httpctx;
        }

        return;
    }

    if (httpctx->curl)
    {
        LOG_debug << "Request already sent using a previous DNS response";
        if (invalidcache && httpctx->isIPv6 == (host->h_addrtype == PF_INET6))
        {
            LOG_warn << "Cancelling request due to the detection of an invalid DNS cache record";
            httpio->cancel(req);
        }
        return;
    }

    // check for fatal errors
    if ((httpio->proxyurl.size() && !httpio->proxyhost.size() && req->method != METHOD_NONE) //malformed proxy string
            || (!httpctx->ares_pending && !httpctx->hostip.size())) // or unable to get the IP for this request
    {
        if (!httpio->proxyinflight || req->method == METHOD_NONE)
        {
            req->status = REQ_FAILURE;
            httpio->statechange = true;

            if (!httpctx->ares_pending && !httpctx->hostip.size())
            {
                LOG_debug << "Unable to get the IP for " << httpctx->hostname;

                // unable to get the IP.
                httpio->inetstatus(false);

                if (status != ARES_EDESTRUCTION)
                {
                    // reinitialize c-ares to prevent permanent hangs
                    httpio->reset = true;
                }
            }

            req->httpiohandle = NULL;

            httpctx->req = NULL;
            if (!httpctx->ares_pending)
            {
                delete httpctx;
            }
        }
        else if(!httpctx->ares_pending)
        {
            httpio->pendingrequests.push(httpctx);
            LOG_debug << "Waiting for the IP of the proxy (1)";
        }

        return;
    }

    bool ares_pending = httpctx->ares_pending != 0;
    if (httpctx->hostip.size())
    {
        LOG_debug << "Name resolution finished";

        // if there is no proxy or we already have the IP of the proxy, send the request.
        // otherwise, queue the request until we get the IP of the proxy
        if (!httpio->proxyurl.size() || httpio->proxyip.size() || req->method == METHOD_NONE)
        {
            send_request(httpctx);
        }
        else if (!httpctx->ares_pending)
        {
            httpio->pendingrequests.push(httpctx);

            if (!httpio->proxyinflight)
            {
                LOG_err << "Unable to get the IP of the proxy";

                // c-ares failed to get the IP of the proxy.
                // queue this request and retry.
                httpio->ipv6proxyenabled = !httpio->ipv6proxyenabled && httpio->ipv6available();
                httpio->request_proxy_ip();
                return;
            }
            else
            {
                LOG_debug << "Waiting for the IP of the proxy (2)";
            }
        }
    }

    if (ares_pending)
    {
        LOG_debug << "Waiting for the completion of the c-ares request";
    }
}
#endif

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

// Generate cash function
// FIXME: make async/make multithreaded
std::string gencash(const string& token, uint8_t easiness)
{
    // Calculate threshold from easiness
    // easiness: encoded threshold (maximum acceptable value in the first 32 byte of the
    // hash (little endian) - the lower, the harder to solve)
    uint32_t threshold =
        static_cast<uint32_t>((((easiness & 63) << 1) + 1) << ((easiness >> 6) * 7 + 3));
    
    // Token is 64 chars in B64, we need the 48 bytes in binary
    string tokenBinary = Base64::atob(token);

    // Buffer to hold 4-byte prefix + 262144 * 48 bytes of the token
    std::vector<uint8_t> buffer(4 + 262144 * 48); // total size = 12582916
    for (auto i = 0; i < 262144; ++i)
    {
        std::copy(tokenBinary.begin(), tokenBinary.end(), buffer.begin() + 4 + i * 48);
    }

    uint32_t* prefixptr = reinterpret_cast<uint32_t*>(buffer.data());

    for (;;)
    {
        // increment prefix (the final result, but not its correctness, will depend on the CPU's endianness)
	// we do not have an explicit abort condition (the actual easiness will be lenient enough)
	(*prefixptr)++;

        // SHA-256 hash
        HashSHA256 hasher;
        hasher.add((const byte*)buffer.data(), static_cast<unsigned>(buffer.size()));

        string hash;
        hasher.get(&hash);

        if (htonl(*(reinterpret_cast<uint32_t*>(hash.data()))) <= threshold)
        {
            // success - return the prefix
            string prefixToReturn(buffer.begin(), buffer.begin() + 4);
            return Base64::btoa(prefixToReturn);
        }
    }
}

void CurlHttpIO::send_request(CurlHttpContext* httpctx)
{
    CurlHttpIO* httpio = httpctx->httpio;
    HttpReq* req = httpctx->req;
    auto len = httpctx->len;
    const char* data = httpctx->data;

    LOG_debug << httpctx->req->logname << req->getMethodString() << " target URL: " << getSafeUrl(req->posturl);

    if (req->binary)
    {
        LOG_debug << httpctx->req->logname << "[sending " << (data ? len : req->out->size()) << " bytes of raw data]";
    }
    else
    {
        if (gLogJSONRequests || req->out->size() < size_t(SimpleLogger::getMaxPayloadLogSize()))
        {
            LOG_debug << httpctx->req->logname << "Sending " << req->out->size() << ": " << DirectMessage(req->out->c_str(), req->out->size())
                      << " (at ds: " << Waiter::ds << ")";
        }
        else
        {
            LOG_debug << httpctx->req->logname << "Sending " << req->out->size() << ": "
                      << DirectMessage(req->out->c_str(), static_cast<size_t>(SimpleLogger::getMaxPayloadLogSize() / 2))
                      << " [...] "
                      << DirectMessage(req->out->c_str() + req->out->size() - SimpleLogger::getMaxPayloadLogSize() / 2, static_cast<size_t>(SimpleLogger::getMaxPayloadLogSize() / 2));
        }
    }

    httpctx->headers = clone_curl_slist(req->type == REQ_JSON ? httpio->contenttypejson : httpio->contenttypebinary);
    httpctx->posturl = req->posturl;

    if (!req->mHashcashToken.empty())
    {
        string nextValue = gencash(req->mHashcashToken, req->mHashcashEasiness);
        string xHashcashHeader{"X-Hashcash: 1:" + req->mHashcashToken + ":" + std::move(nextValue)};
        httpctx->headers = curl_slist_append(httpctx->headers, xHashcashHeader.c_str());
        LOG_warn << "X-Hashcash computed: " << xHashcashHeader;
        req->mHashcashToken.clear();
    }

#ifdef MEGA_USE_C_ARES
    if(httpio->proxyip.size())
    {
        NET_debug << "Using the hostname instead of the IP";
    }
    else if(httpctx->hostip.size())
    {
        NET_debug << "Using the IP of the hostname: " << httpctx->hostip;
        httpctx->posturl.replace(httpctx->posturl.find(httpctx->hostname), httpctx->hostname.size(), httpctx->hostip);
        httpctx->headers = curl_slist_append(httpctx->headers, httpctx->hostheader.c_str());
    }
    else
    {
        LOG_err << "No IP nor proxy available";
        req->status = REQ_FAILURE;
        req->httpiohandle = NULL;
        curl_slist_free_all(httpctx->headers);

        httpctx->req = NULL;
        if (!httpctx->ares_pending)
        {
            delete httpctx;
        }
        httpio->statechange = true;
        return;
    }
#endif

    CURL* curl;
    if ((curl = curl_easy_init()))
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
#ifndef MEGA_USE_C_ARES
        curl_easy_setopt(curl, CURLOPT_QUICK_EXIT, 1L);
#endif

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
            if (curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY,
                  !memcmp(req->posturl.data(), httpio->APIURL.data(), httpio->APIURL.size())
                  || !memcmp(req->posturl.data(), MegaClient::REQSTATURL.data(), MegaClient::REQSTATURL.size())
                    ? "sha256//0W38e765pAfPqS3DqSVOrPsC4MEOvRBaXQ7nY1AJ47E=;" //API 1
                      "sha256//gSRHRu1asldal0HP95oXM/5RzBfP1OIrPjYsta8og80="  //API 2
                    : (!memcmp(req->posturl.data(), MegaClient::SFUSTATSURL.data(), MegaClient::SFUSTATSURL.size()))
                           ? "sha256//2ZAltznnzY3Iee3NIZPOgqIQVNXVjvDEjWTmAreYVFU=;"  // STATSSFU  1
                             "sha256//7jLrvaEtfqTCHew0iibvEm2k61iatru+rwhFD7g3nxA="   // STATSSFU  2
                                 : nullptr) ==  CURLE_OK)
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

#ifdef MEGA_USE_C_ARES
        if (!httpctx->ares_pending)
#endif
        {
            delete httpctx;
        }
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

#ifndef MEGA_USE_C_ARES
    send_request(httpctx);
#else
    httpctx->ares_pending = 1;

    if (ipv6proxyenabled)
    {
        httpctx->ares_pending++;
        NET_debug << "Resolving IPv6 address for proxy: " << proxyhost;
        ares_gethostbyname(ares, proxyhost.c_str(), PF_INET6, proxy_ready_callback, httpctx);
    }

    NET_debug << "Resolving IPv4 address for proxy: " << proxyhost;
    ares_gethostbyname(ares, proxyhost.c_str(), PF_INET, proxy_ready_callback, httpctx);
#endif
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
        NET_verbose << (debugdata ? static_cast<HttpReq*>(debugdata)->logname : string()) << "cURL: " << data << errnoInfo;
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
#ifdef MEGA_USE_C_ARES
    httpctx->ares_pending = 0;
#endif
    httpctx->d = (req->type == REQ_JSON || req->method == METHOD_NONE) ? API : ((data ? len : req->out->size()) ? PUT : GET);
    req->httpiohandle = (void*)httpctx;

    bool validrequest = true;
    if ((proxyurl.size() && !proxyhost.size()) // malformed proxy string
            || !(validrequest = crackurl(&req->posturl, &httpctx->scheme, &httpctx->hostname, &httpctx->port))) // invalid request
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

#ifdef MEGA_USE_C_ARES
    if (reset)
    {
        LOG_debug << "Error in c-ares. Reinitializing...";
        reset = false;
        ares_destroy(ares);
        struct ares_options options;
        options.tries = 2;
        ares_init_options(&ares, &options, ARES_OPT_TRIES);

        if (dnsservers.size())
        {
            LOG_info << "Using custom DNS servers: " << dnsservers;
            ares_set_servers_csv(ares, dnsservers.c_str());
        }
        else if (!dnsok)
        {
            getMEGADNSservers(&dnsservers, false);
            ares_set_servers_csv(ares, dnsservers.c_str());
        }

        if (proxyurl.size() && !proxyip.size())
        {
            LOG_debug << "Unresolved proxy name. Resolving...";
            request_proxy_ip();
        }
    }
#endif

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


#ifdef MEGA_USE_C_ARES
    httpctx->ares_pending = 1;
#endif

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
#ifdef MEGA_USE_C_ARES
            httpctx->ares_pending = 0;
#endif
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
#ifdef MEGA_USE_C_ARES
        httpctx->ares_pending = 0;
#endif
        send_request(httpctx);
        return;
    }

#ifndef MEGA_USE_C_ARES
    send_request(httpctx);
#else
    if (ipv6requestsenabled)
    {
        httpctx->ares_pending++;
        NET_debug << "Resolving IPv6 address for " << httpctx->hostname;
        ares_gethostbyname(ares, httpctx->hostname.c_str(), PF_INET6, ares_completed_callback, httpctx);
    }

    NET_debug << "Resolving IPv4 address for " << httpctx->hostname;
    ares_gethostbyname(ares, httpctx->hostname.c_str(), PF_INET, ares_completed_callback, httpctx);
#endif
}

void CurlHttpIO::setproxy(Proxy* proxy)
{
    // clear the previous proxy IP
    proxyip.clear();

    if (proxy->getProxyType() != Proxy::CUSTOM || !proxy->getProxyURL().size())
    {
        // automatic proxy is not supported
        // invalidate inflight proxy changes
        proxyscheme.clear();
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

        if ((req->status == REQ_FAILURE || httpctx->curl)
#ifdef MEGA_USE_C_ARES
            && !httpctx->ares_pending
#endif
            )
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
        curl_easy_getinfo(httpctx->curl, CURLINFO_SIZE_UPLOAD_T, &bytes);
    }

    return (m_off_t)bytes;
}

// process events
bool CurlHttpIO::doio()
{
    bool result;
    statechange = false;

#ifdef MEGA_USE_C_ARES
    processaresevents();
#endif

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

    while ((msg = curl_multi_info_read(curlmhandle, &dummy)))
    {
        HttpReq* req = NULL;
        if (curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (char**)&req) == CURLE_OK && req)
        {
            req->httpio = NULL;

            if (msg->msg == CURLMSG_DONE)
            {
                measureLatency(msg->easy_handle, req);

                CURLcode errorCode = msg->data.result;
                if (errorCode != CURLE_OK && errorCode != CURLE_HTTP_RETURNED_ERROR && errorCode != CURLE_WRITE_ERROR)
                {
                    LOG_debug << req->logname << "CURLMSG_DONE with error " << errorCode << ": " << curl_easy_strerror(errorCode);

                #if LIBCURL_VERSION_NUM >= 0x072c00 // At least cURL 7.44.0
                    if (errorCode == CURLE_SSL_PINNEDPUBKEYNOTMATCH)
                    {
                        pkpErrors++;
                        LOG_warn << req->logname << "Invalid public key?";

                        if (pkpErrors == 3)
                        {
                            pkpErrors = 0;

                            LOG_err << req->logname << "Invalid public key. Possible MITM attack!!";
                            req->sslcheckfailed = true;

                            struct curl_certinfo *ci;
                            if (curl_easy_getinfo(msg->easy_handle, CURLINFO_CERTINFO, &ci) == CURLE_OK)
                            {
                                LOG_warn << req->logname << "Fake SSL certificate data:";
                                for (int i = 0; i < ci->num_of_certs; i++)
                                {
                                    struct curl_slist *slist = ci->certinfo[i];
                                    while (slist)
                                    {
                                        LOG_warn << req->logname << i << ": " << slist->data;
                                        if (i == 0 && !memcmp("Issuer:", slist->data, 7))
                                        {
                                            const char *issuer = NULL;
                                            if ((issuer = strstr(slist->data, "CN = ")))
                                            {
                                                issuer += 5;
                                            }
                                            else if ((issuer = strstr(slist->data, "CN=")))
                                            {
                                                issuer += 3;
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
                                    LOG_debug << req->logname << "Fake certificate issuer: " << req->sslfakeissuer;
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

                LOG_debug << req->logname << "CURLMSG_DONE with HTTP status: " << req->httpstatus << " from "
                          << (req->httpiohandle ? (((CurlHttpContext*)req->httpiohandle)->hostname + " - " + ((CurlHttpContext*)req->httpiohandle)->hostip) : "(unknown) ");
                if (req->httpstatus)
                {
                    if (req->mExpectRedirect && req->isRedirection()) // HTTP 3xx response
                    {
                        char *url = NULL;
                        curl_easy_getinfo(msg->easy_handle, CURLINFO_REDIRECT_URL, &url);
                        if (url)
                        {
                            req->mRedirectURL = url;
                            LOG_debug << req->logname << "Redirected to " << req->mRedirectURL;
                        }
                    }

                    if (req->method == METHOD_NONE && req->httpiohandle)
                    {
                        char *ip = NULL;
                        CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;
                        if (curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIMARY_IP, &ip) == CURLE_OK
                              && ip && !strstr(httpctx->hostip.c_str(), ip))
                        {
                            LOG_err << req->logname << "cURL has changed the original IP! " << httpctx ->hostip << " -> " << ip;
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
                        LOG_debug << req->logname << "[received " << (req->buf ? req->bufpos : (int)req->in.size()) << " bytes of raw data]";
                    }
                    else if (req->mChunked && static_cast<size_t>(req->bufpos) != req->in.size())
                    {
                        LOG_debug << req->logname << "[received " << req->bufpos << " bytes of chunked data]";
                    }
                    else
                    {
                        if (gLogJSONRequests ||
                            req->in.size() < size_t(SimpleLogger::getMaxPayloadLogSize()))
                        {
                            LOG_debug << req->logname << "Received " << req->in.size() << ": " << DirectMessage(req->in.c_str(), req->in.size())
                                      << " (at ds: " << Waiter::ds << ")";
                        }
                        else
                        {
                            LOG_debug << req->logname << "Received " << req->in.size() << ": "
                                      << DirectMessage(req->in.c_str(), static_cast<size_t>(SimpleLogger::getMaxPayloadLogSize() / 2))
                                      << " [...] "
                                      << DirectMessage(req->in.c_str() + req->in.size() - SimpleLogger::getMaxPayloadLogSize() / 2, static_cast<size_t>(SimpleLogger::getMaxPayloadLogSize() / 2));
                        }
                    }
                }

                // check httpstatus, redirecturl and response length
                req->status = ((req->httpstatus == 200 || (req->mExpectRedirect && req->isRedirection() && req->mRedirectURL.size()))
                               && errorCode != CURLE_PARTIAL_FILE
                               && (req->contentlength < 0
                                   || req->contentlength == ((req->buf || req->mChunked) ? req->bufpos : (int)req->in.size())))
                        ? REQ_SUCCESS : REQ_FAILURE;

                if (req->status == REQ_SUCCESS)
                {
                    dnsok = true;
                    lastdata = Waiter::ds;
                    req->lastdata = Waiter::ds;
                }
                else
                {
                    LOG_warn << req->logname << "REQ_FAILURE."
                             << " Status: " << req->httpstatus
                             << " CURLcode: " << errorCode
                             << "  Content-Length: " << req->contentlength
                             << "  buffer? " << (req->buf != NULL)
                             << "  bufferSize: " << (req->buf ? req->bufpos : (int)req->in.size());
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
                        if ((dnsEntry.ipv4.size() && !dnsEntry.isIPv4Expired())
                                || (!httpctx->isCachedIp
#ifdef MEGA_USE_C_ARES
                                    && httpctx->ares_pending
#endif
                                    ))
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
                                LOG_debug << req->logname << "Retrying using IPv4 from cache";
                                httpctx->isIPv6 = false;
                                httpctx->hostip = dnsEntry.ipv4;
                                send_request(httpctx);
                            }
                            else
                            {
                                httpctx->hostip.clear();
                                LOG_debug << req->logname << "Retrying with the pending DNS response";
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
#ifdef MEGA_USE_C_ARES
                if (!httpctx->ares_pending)
#endif
                {
                    delete httpctx;
                }
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

        LOG_verbose << "Connect time and start transfer latency for request " << req->logname
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
#ifdef MEGA_USE_C_ARES
        if (!httpctx->ares_pending)
#endif
        {
            delete httpctx;
        }
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
                nread = maxbytes;
            }
            httpio->partialdata[PUT] += nread;
        }
    }

    memcpy(ptr, buf, nread);
    req->outpos += nread;
    //LOG_debug << req->logname << "Supplying " << nread << " bytes to cURL to send";
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
                    CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;
                    httpio->pausedrequests[GET].insert(httpctx->curl);
                    httpio->arerequestspaused[GET] = true;
                    return CURL_WRITEFUNC_PAUSE;
                }
                httpio->partialdata[GET] += len;
            }
        }

        if (len)
        {
            req->put(ptr, len, true);
        }

        httpio->lastdata = Waiter::ds;
        req->lastdata = Waiter::ds;
    }

    return len;
}

// set contentlength according to Original-Content-Length header
size_t CurlHttpIO::check_header(void* ptr, size_t size, size_t nmemb, void* target)
{
    HttpReq *req = (HttpReq*)target;
    size_t len = size * nmemb;
    if (len > 2)
    {
        NET_verbose << req->logname << "Header: " << string((const char *)ptr, len - 2);
    }

    if (len > 5 && !memcmp(ptr, "HTTP/", 5))
    {
        if (req->contentlength >= 0)
        {
            // For authentication with some proxies, cURL sends two requests in the context of a single one
            // Content-Length is reset here to not take into account the header from the first response

            LOG_warn << "Receiving a second response. Resetting Content-Length";
            req->contentlength = -1;
        }

        return size * nmemb;
    }
    else if (len > 15 && !memcmp(ptr, "Content-Length:", 15))
    {
        if (req->contentlength < 0)
        {
            req->setcontentlength(atoll((char*)ptr + 15));
        }
    }
    else if (len > 24 && !memcmp(ptr, "Original-Content-Length:", 24))
    {
        req->setcontentlength(atoll((char*)ptr + 24));
    }
    else if (len > 17 && !memcmp(ptr, "X-MEGA-Time-Left:", 17))
    {
        req->timeleft = atol((char*)ptr + 17);
    }
    else if (len > 15 && !memcmp(ptr, "Content-Type:", 13))
    {
        req->contenttype.assign((char *)ptr + 13, len - 15);
    }
    else if (len >= (11 + 7) && !memcmp(ptr, "X-Hashcash:", 11))
    {
        // trim trailing CRLF
        while (len > 11 && static_cast<uint8_t*>(ptr)[len - 1] < ' ') len--;

        string buffer{(char*)ptr + 11, len - 11};
        LOG_warn << "X-Hashcash received:" << buffer;

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
            hc.push_back(move(buf));
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
        newoffset = req->outpos + offset;
        break;
    case SEEK_END:
        newoffset = totalsize + offset;
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

// This one was causing us to issue additional c-ares requests, when normal usage already sends those requests
// CURL doco: When set, this callback function gets called by libcurl when the socket has been created, but before the connect call to allow applications to change specific socket options.The callback's purpose argument identifies the exact purpose for this particular socket:

int CurlHttpIO::sockopt_callback(void *clientp, curl_socket_t, curlsocktype)
{
#ifdef MEGA_USE_C_ARES
    HttpReq *req = (HttpReq*)clientp;
    CurlHttpIO* httpio = (CurlHttpIO*)req->httpio;
    CurlHttpContext* httpctx = (CurlHttpContext*)req->httpiohandle;
    if (httpio && !httpio->disconnecting
            && httpctx && httpctx->isCachedIp && !httpctx->ares_pending && httpio->dnscache[httpctx->hostname].mNeedsResolvingAgain)
    {
        httpio->dnscache[httpctx->hostname].mNeedsResolvingAgain = false;
        httpctx->ares_pending = 1;

        if (httpio->ipv6requestsenabled)
        {
            httpctx->ares_pending++;
            LOG_debug << "Resolving IPv6 address for " << httpctx->hostname << " during connection";
            ares_gethostbyname(httpio->ares, httpctx->hostname.c_str(), PF_INET6, ares_completed_callback, httpctx);
        }

        LOG_debug << "Resolving IPv4 address for " << httpctx->hostname << " during connection";
        ares_gethostbyname(httpio->ares, httpctx->hostname.c_str(), PF_INET, ares_completed_callback, httpctx);
    }
#endif

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
            if ((!memcmp(request->posturl.data(), httpio->APIURL.data(), httpio->APIURL.size())
                    && (!memcmp(buf, APISSLMODULUS1, sizeof APISSLMODULUS1 - 1) || !memcmp(buf, APISSLMODULUS2, sizeof APISSLMODULUS2 - 1)))
                ||(!memcmp(request->posturl.data(), MegaClient::SFUSTATSURL.data(), MegaClient::SFUSTATSURL.size())
                    && (!memcmp(buf, SFUSTATSSSLMODULUS, sizeof SFUSTATSSSLMODULUS - 1) || !memcmp(buf, SFUSTATSSSLMODULUS2, sizeof SFUSTATSSSLMODULUS2 - 1)))
                )
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
            request->sslfakeissuer.resize(len > 0 ? len : 0);
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

#ifdef MEGA_USE_C_ARES
#if (defined(ANDROID) || defined(__ANDROID__)) && ARES_VERSION >= 0x010F00

void CurlHttpIO::initialize_android()
{
    bool initialized = ares_library_android_initialized() == ARES_SUCCESS;
    if (initialized)
    {
            LOG_warn << "initialize_android: already initialized";
            crashlytics_log("initialize_android: already initialized");
            return;
    }

    if (!MEGAjvm)
    {
        LOG_err << "No JVM found";
        crashlytics_log("No JVM found");
        return;
    }

    bool detach = false;
    try
    {
        JNIEnv *env;
        int result = MEGAjvm->GetEnv((void **)&env, JNI_VERSION_1_6);
        if (result == JNI_EDETACHED)
        {
            if (MEGAjvm->AttachCurrentThread(&env, NULL) != JNI_OK)
            {
                LOG_err << "Unable to attach the current thread";
                crashlytics_log("Unable to attach the current thread");
                return;
            }
            detach = true;
        }
        else if (result != JNI_OK)
        {
            LOG_err << "Unable to get JNI environment";
            crashlytics_log("Unable to get JNI environment");
            return;
        }

        jclass appGlobalsClass = env->FindClass("android/app/AppGlobals");
        if (!appGlobalsClass)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get android/app/AppGlobals";
            crashlytics_log("Failed to get android/app/AppGlobals");
            if (detach)
            {
                MEGAjvm->DetachCurrentThread();
            }
            return;
        }

        jmethodID getInitialApplicationMID = env->GetStaticMethodID(appGlobalsClass,"getInitialApplication","()Landroid/app/Application;");
        if (!getInitialApplicationMID)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get getInitialApplication()";
            crashlytics_log("Failed to get getInitialApplication()");
            if (detach)
            {
                MEGAjvm->DetachCurrentThread();
            }
            return;
        }

        jobject context = env->CallStaticObjectMethod(appGlobalsClass, getInitialApplicationMID);
        if (!context)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get context";
            crashlytics_log("Failed to get context");
            if (detach)
            {
                MEGAjvm->DetachCurrentThread();
            }
            return;
        }

        jclass contextClass = env->FindClass("android/content/Context");
        if (!contextClass)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get android/content/Context";
            crashlytics_log("Failed to get android/content/Context");
            if (detach)
            {
                MEGAjvm->DetachCurrentThread();
            }
            return;
        }

        jmethodID getSystemServiceMID = env->GetMethodID(contextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        if (!getSystemServiceMID)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get getSystemService()";
            crashlytics_log("Failed to get getSystemService()");
            if (detach)
            {
                MEGAjvm->DetachCurrentThread();
            }
            return;
        }

        jfieldID fid = env->GetStaticFieldID(contextClass, "CONNECTIVITY_SERVICE", "Ljava/lang/String;");
        if (!fid)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get CONNECTIVITY_SERVICE";
            crashlytics_log("Failed to get CONNECTIVITY_SERVICE");
            if (detach)
            {
                MEGAjvm->DetachCurrentThread();
            }
            return;
        }

        jstring str = (jstring)env->GetStaticObjectField(contextClass, fid);
        if (!str)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get CONNECTIVITY_SERVICE value";
            crashlytics_log("Failed to get CONNECTIVITY_SERVICE value");
            if (detach)
            {
                MEGAjvm->DetachCurrentThread();
            }
            return;
        }

        jobject connectivityManager = env->CallObjectMethod(context, getSystemServiceMID, str);
        if (!connectivityManager)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get connectivityManager";
            crashlytics_log("Failed to get connectivityManager");
            if (detach)
            {
                MEGAjvm->DetachCurrentThread();
            }
            return;
        }

        // ares_library_init_jvm(MEGAjvm); --> already done at JNI_OnLoad()
        ares_library_init_android(connectivityManager);
        initialized = ares_library_android_initialized() == ARES_SUCCESS;
        assert(initialized);
        if (!initialized)
        {
            crashlytics_log("Failed to initialize c-ares for Android");
        }

        if (detach)
        {
            MEGAjvm->DetachCurrentThread();
        }
    }
    catch (...)
    {
        try
        {
            if (detach)
            {
                MEGAjvm->DetachCurrentThread();
            }
        }
        catch (...) { }
    }
}
#endif
#endif

} // namespace
