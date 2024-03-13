#pragma once

#include <poll.h>

#include <system_error>
#include <chrono>
#include <vector>
#include <filesystem>

namespace mega {
namespace gfx {

class SocketUtils
{
public:
    static std::filesystem::path toSocketPath(const std::string& name);

    static std::pair<std::error_code, int> listen(const std::string& name);

    static std::pair<std::error_code, int> accept(int listeningFd, std::chrono::milliseconds timeout);

    //
    // Read until count bytes or timeout
    //
    static std::error_code read(int fd, void* buf, size_t count, std::chrono::milliseconds timeout);

    static std::error_code write(int fd, const void* data, size_t n, std::chrono::milliseconds timeout);

private:
    static bool isRetryErrorNo(int errorNo);

    static bool isPollError(int event);

    static std::error_code poll(std::vector<struct pollfd> fds, std::chrono::milliseconds timeout);

    static std::error_code pollFd(int fd, short events, std::chrono::milliseconds timeout);

    static std::error_code pollForRead(int fd, std::chrono::milliseconds timeout);

    static std::error_code pollForWrite(int fd, std::chrono::milliseconds timeout);

    static std::error_code pollForAccept(int fd, std::chrono::milliseconds timeout);

    static std::error_code doBindAndListen(int fd, const std::string& fullPath);

    static constexpr size_t maxSocketPathLength();
};

} // end of namespace
}