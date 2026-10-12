#ifndef INCLUDE_TESTS_TCP_BLACKHOLE_LISTENER_H_
#define INCLUDE_TESTS_TCP_BLACKHOLE_LISTENER_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sdk_test
{

/**
 * @brief Loopback TCP endpoint that accepts every connection and never reads or writes a byte.
 *
 * Models a black-holed storage node or proxy: the TCP handshake completes, so a client moves on
 * to its TLS / HTTP-upgrade phase and blocks there until its own timeout fires. Binds 127.0.0.1
 * on an ephemeral port (safe under parallel test instances). Accepted sockets are held open
 * until close()/destruction so the peer never observes EOF or RST.
 */
class TcpBlackholeListener
{
public:
    TcpBlackholeListener();
    ~TcpBlackholeListener();

    TcpBlackholeListener(const TcpBlackholeListener&) = delete;
    TcpBlackholeListener& operator=(const TcpBlackholeListener&) = delete;

    // False when the listening socket could not be created, bound or put into listen state.
    bool listening() const
    {
        return mListening;
    }

    std::uint16_t port() const
    {
        return mPort;
    }

    // Convenience: "<scheme>://127.0.0.1:<port><path>", e.g. url("wss", "/ul/blackhole").
    std::string url(const std::string& scheme, const std::string& path) const;

    // Blocks until at least minCount connections have been accepted or the timeout elapses.
    bool waitForAccept(std::chrono::milliseconds timeout, std::size_t minCount = 1) const;

    std::size_t acceptedCount() const;

    // Stops accepting and closes the listening socket plus every held connection (peers now
    // see EOF/RST and fail fast). Idempotent; the destructor calls it.
    void close();

private:
    void acceptLoop();

    // Native socket handles are kept as std::intptr_t so this header stays free of
    // platform socket includes (int on POSIX, SOCKET on Windows).
    // Windows only: WSAStartup succeeded (paired with WSACleanup in the destructor).
    [[maybe_unused]] bool mWinsockStarted{false};
    std::intptr_t mListenSocket{-1};
    std::uint16_t mPort{0};
    bool mListening{false};
    std::atomic<bool> mStop{false};
    std::thread mAcceptThread;

    mutable std::mutex mMutex;
    mutable std::condition_variable mCv;
    std::vector<std::intptr_t> mAccepted; // guarded by mMutex
    std::size_t mAcceptedCount{0}; // guarded by mMutex
};

} // namespace sdk_test

#endif // INCLUDE_TESTS_TCP_BLACKHOLE_LISTENER_H_
