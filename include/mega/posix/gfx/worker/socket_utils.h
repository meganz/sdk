#pragma once

#include <poll.h>

#include <system_error>
#include <chrono>
#include <vector>
#include <filesystem>

namespace mega {
namespace gfx {

struct SocketUtils
{
    static std::filesystem::path toSocketPath(const std::string& name);

    static std::pair<std::error_code, int> listen(const std::filesystem::path& socketPath);

    static std::pair<std::error_code, int> accept(int listeningFd, std::chrono::milliseconds timeout);

    //
    // Read until count bytes or timeout
    //
    static std::error_code read(int fd, void* buf, size_t count, std::chrono::milliseconds timeout);

    //
    // Write all n bytes in data or timeout
    //
    static std::error_code write(int fd, const void* data, size_t n, std::chrono::milliseconds timeout);
};

} // end of namespace
}