#pragma once
#include "mega/types.h"
#include "mega/gfx/worker/comms.h"

#include <poll.h>

#include <system_error>
#include <chrono>
#include <filesystem>
namespace mega {
namespace gfx {

class Socket : public IEndpoint
{
public:
    Socket(int socket, const std::string& name) : mSocket(socket), mName(name) {}

    Socket(const Socket&) = delete;

    Socket(Socket&& other);

    ~Socket();

    bool isValid() const { return mSocket >= 0; }

    int fd() const { return mSocket; }
protected:
    enum class Type
    {
        Client,
        Server
    };

    // file descriptor to the socket
    int mSocket{-1};

    std::string mName;

private:
    bool doWrite(const void* data, size_t n, TimeoutMs timeout) override;

    bool doRead(void* data, size_t n, TimeoutMs timeout) override;

    //virtual Type type() const = 0;
};

class SocketUtils
{
public:
    static std::pair<std::error_code, int> accept(int listeningFd, std::chrono::milliseconds timeout);

    static std::filesystem::path toSocketPath(const std::string& name);

    //
    // Read until count bytes or timeout
    //
    static std::error_code read(int fd, void* buf, size_t count, std::chrono::milliseconds timeout);

    static std::error_code write(int fd, const void* data, size_t n, std::chrono::milliseconds timeout);

    static std::unique_ptr<Socket> listen(const std::string& name);

private:
    static bool isRetryErrorNo(int errorNo);

    static bool isPollError(int event);

    static std::error_code poll(std::vector<struct pollfd> fds, std::chrono::milliseconds timeout);

    static std::error_code pollFd(int fd, short events, std::chrono::milliseconds timeout);

    static std::error_code pollForRead(int fd, std::chrono::milliseconds timeout);

    static std::error_code pollForWrite(int fd, std::chrono::milliseconds timeout);

    static std::error_code pollForAccept(int fd, std::chrono::milliseconds timeout);
};

} // end of namespace
}