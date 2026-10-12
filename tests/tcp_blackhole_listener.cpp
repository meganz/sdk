#include "tcp_blackhole_listener.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <poll.h>
#include <unistd.h>
#endif

namespace sdk_test
{

namespace
{

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

void closeNative(NativeSocket s)
{
    ::closesocket(s);
}

int pollReadable(NativeSocket s, int timeoutMs)
{
    WSAPOLLFD pfd{};
    pfd.fd = s;
    pfd.events = POLLRDNORM;
    return ::WSAPoll(&pfd, 1, timeoutMs);
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;

void closeNative(NativeSocket s)
{
    ::close(s);
}

int pollReadable(NativeSocket s, int timeoutMs)
{
    pollfd pfd{};
    pfd.fd = s;
    pfd.events = POLLIN;
    return ::poll(&pfd, 1, timeoutMs);
}
#endif

NativeSocket toNative(std::intptr_t s)
{
    return static_cast<NativeSocket>(s);
}

std::intptr_t fromNative(NativeSocket s)
{
    return static_cast<std::intptr_t>(s);
}

// Poll granularity of the accept loop; bounds how long close() waits for the thread.
constexpr int kAcceptPollMs = 100;

} // namespace

TcpBlackholeListener::TcpBlackholeListener()
{
#ifdef _WIN32
    WSADATA wsaData{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        return;
    }
    mWinsockStarted = true;
#endif
    const NativeSocket s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == kInvalidSocket)
    {
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // ephemeral
    const socklen_t addrLen = sizeof(addr);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), addrLen) != 0 ||
        ::listen(s, SOMAXCONN) != 0)
    {
        closeNative(s);
        return;
    }

    sockaddr_in bound{};
    socklen_t boundLen = sizeof(bound);
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &boundLen) != 0)
    {
        closeNative(s);
        return;
    }

    mPort = ntohs(bound.sin_port);
    mListenSocket = fromNative(s);
    mListening = true;
    mAcceptThread = std::thread(
        [this]
        {
            acceptLoop();
        });
}

TcpBlackholeListener::~TcpBlackholeListener()
{
    close();
#ifdef _WIN32
    if (mWinsockStarted)
    {
        ::WSACleanup();
    }
#endif
}

std::string TcpBlackholeListener::url(const std::string& scheme, const std::string& path) const
{
    return scheme + "://127.0.0.1:" + std::to_string(mPort) + path;
}

bool TcpBlackholeListener::waitForAccept(std::chrono::milliseconds timeout,
                                         std::size_t minCount) const
{
    std::unique_lock<std::mutex> lock(mMutex);
    return mCv.wait_for(lock,
                        timeout,
                        [this, minCount]
                        {
                            return mAcceptedCount >= minCount;
                        });
}

std::size_t TcpBlackholeListener::acceptedCount() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mAcceptedCount;
}

void TcpBlackholeListener::close()
{
    mStop.store(true, std::memory_order_release);
    if (mAcceptThread.joinable())
    {
        mAcceptThread.join();
    }
    if (mListening)
    {
        closeNative(toNative(mListenSocket));
        mListenSocket = -1;
        mListening = false;
    }
    std::vector<std::intptr_t> accepted;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        accepted.swap(mAccepted);
    }
    for (const auto s: accepted)
    {
        closeNative(toNative(s));
    }
}

void TcpBlackholeListener::acceptLoop()
{
    const NativeSocket listenSocket = toNative(mListenSocket);
    while (!mStop.load(std::memory_order_acquire))
    {
        if (pollReadable(listenSocket, kAcceptPollMs) <= 0)
        {
            continue; // timeout or transient error: re-check mStop
        }
        const NativeSocket accepted = ::accept(listenSocket, nullptr, nullptr);
        if (accepted == kInvalidSocket)
        {
            // e.g. EMFILE with the connection still queued: never re-poll without a delay.
            std::this_thread::sleep_for(std::chrono::milliseconds(kAcceptPollMs));
            continue;
        }
        // Hold the connection open and stay silent: never read, never write.
        std::lock_guard<std::mutex> lock(mMutex);
        mAccepted.push_back(fromNative(accepted));
        ++mAcceptedCount;
        mCv.notify_all();
    }
}

} // namespace sdk_test
