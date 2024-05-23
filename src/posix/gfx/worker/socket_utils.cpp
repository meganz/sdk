#include "mega/posix/gfx/worker/socket_utils.h"
#include "mega/logging.h"
#include "mega/scoped_timer.h"
#include <sys/poll.h>

#include <cassert>
#include <filesystem>
#include <poll.h>

#include <chrono>
#include <system_error>
#include <unistd.h>

using std::chrono::milliseconds;
using std::error_code;
using std::chrono::duration_cast;
using std::system_category;

namespace fs = std::filesystem;

namespace
{

// Refer man pages for read, write, accept and etc.. For these errors, we can/shall retry.
bool isRetryErrorNo(int errorNo)
{
    return errorNo == EAGAIN || errorNo == EWOULDBLOCK || errorNo == EINTR;
}

// Refer man page for poll
bool isPollError(int event)
{
    return event & (POLLERR | POLLHUP | POLLNVAL);
}

// Poll a group of file descriptors. It deals with EINTR.
error_code poll(std::vector<struct pollfd> fds, milliseconds timeout)
{
    const mega::ScopedSteadyTimer timer;
    milliseconds                  remaining{timeout};  //Remaining timeout in case of EINTR
    int                           ret = 0;
    do
    {
        ret = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), static_cast<int>(timeout.count()));
        remaining -= duration_cast<milliseconds>(timer.passedTime());
    } while (ret < 0 && errno == EINTR && remaining > milliseconds{0});

    if (ret < 0)
    {
        LOG_err << "Failed to poll: " << errno;
        return error_code{errno, system_category()};
    }

    if (ret == 0)
    {
        return error_code{ETIMEDOUT, system_category()};
    }

    return error_code{};
}

// Poll a single file descriptor
error_code pollFd(int fd, short events, milliseconds timeout)
{
    // Poll
    std::vector<struct pollfd> fds{
        {.fd = fd, .events = events, .revents = 0}
    };

    if (auto errorCode = poll(fds, timeout))
    {
        return errorCode;
    }

    // check if the poll returns an error event
    const auto& polledFd = fds.front();
    if (isPollError(polledFd.revents))
    {
        return error_code{ECONNABORTED, system_category()};
    }

    return error_code{};
}

error_code pollForRead(int fd, milliseconds timeout)
{
    return pollFd(fd, POLLIN, timeout);
}

error_code pollForWrite(int fd, milliseconds timeout)
{
    return pollFd(fd, POLLOUT, timeout);
}

error_code pollForAccept(int fd, milliseconds timeout)
{
    return pollFd(fd, POLLIN, timeout);
}

constexpr size_t maxSocketPathLength()
{
    return sizeof(sockaddr_un::sun_path);
}

// The caller should check that the socketPath does not exceed the capability of sun_path
// to avoid truncation
sockaddr_un initSocketAddr(const std::string& socketPath)
{
    // Zero initialization with the address family value AF_UNIX
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;

    // Calculate the count for copying and leave one for null
    size_t count = std::min(socketPath.size(), maxSocketPathLength() - 1);

    std::copy_n(socketPath.begin(), count, addr.sun_path);

    return addr;
}

// Bind the fd on the socketPath and listen on it
error_code doBindAndListen(int fd, const std::string& socketPath)
{
    constexpr int QUEUE_LEN = 10;

    // Extra 1 for null terminated
    if (socketPath.size() + 1 > maxSocketPathLength())
    {
        LOG_err << "Unix domain socket name is too long, " << socketPath;
        return error_code{ENAMETOOLONG, system_category()};
    }

    auto addr = initSocketAddr(socketPath);

    // Bind name
    if (::bind(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)) == -1)
    {
        LOG_err << "Failed to bind UNIX domain socket name: " << socketPath << " errno: " << errno;
        return error_code{errno, system_category()};
    }

    // Listen
    if (::listen(fd, QUEUE_LEN) < 0)
    {
        LOG_err << "Failed to listen UNIX domain socket name: " << socketPath << " errno: " << errno;
        return error_code{errno, system_category()};
    }

    // Success
    return error_code{};
}

std::error_code createDirectories(const fs::path& directory)
{
    std::error_code errorCode;
    fs::create_directories(directory, errorCode);
    return errorCode;
}

std::error_code removePath(const fs::path& path)
{
    std::error_code errorCode;
    fs::remove(path, errorCode);
    return errorCode;
}

}
namespace mega {
namespace gfx {

fs::path SocketUtils::toSocketPath(const std::string& name)
{
    return fs::path{"/tmp"} / ("MegaLimited" + std::to_string(getuid()))  / name;
}

std::error_code SocketUtils::removeSocketFile(const std::string& name)
{
    return removePath(toSocketPath(name));
}

std::pair<error_code, int> SocketUtils::accept(int listeningFd, milliseconds timeout)
{
    do
    {
        const auto errorCode = pollForAccept(listeningFd, timeout);
        if (errorCode)
        {
            return {errorCode, -1};
        }

        const auto dataSocket = ::accept(listeningFd, nullptr, nullptr);

        // Success
        if (dataSocket >= 0)
        {
            return {error_code{}, dataSocket};
        }

        assert(dataSocket < 0);

        // None retry errors
        if (!isRetryErrorNo(errno))
        {
            return {error_code{errno, system_category()}, -1};
        }

        // Retry errors
        assert(isRetryErrorNo(errno));
    } while (true);
}

error_code SocketUtils::write(int fd, const void* data, size_t n, milliseconds timeout)
{
    size_t offset = 0;
    while (offset < n)
    {
        // Poll
        if (const auto errorCode = pollForWrite(fd, timeout))
        {
            LOG_err << "Failed to pollForWrite, " << errorCode.message();
            return errorCode;
        }

        // Write
        const size_t remaining = n - offset;
        const ssize_t written = ::write(fd, static_cast<const char *>(data) + offset, remaining);

        // Non retry errors
        if (written < 0 && !isRetryErrorNo(errno))
        {
            LOG_err << "Failed to write, errno: " << errno;
            return error_code{errno, system_category()};
        }

        // Retry errors
        if (written < 0 && isRetryErrorNo(errno))
        {
            continue;
        }

        // Success
        assert(written >= 0);
        offset += static_cast<size_t>(written);
    }

    return error_code{};
}

error_code SocketUtils::read(int fd, void* buf, size_t n, milliseconds timeout)
{
    size_t offset = 0;
    while (offset < n)
    {
        // Poll
        if (const auto errorCode = pollForRead(fd, timeout))
        {
            LOG_err << "Failed to pollForRead, " << errorCode.message();
            return errorCode;
        }

        // Read
        const size_t remaining = n - offset;
        const ssize_t bytesRead = ::read(fd, static_cast<char *>(buf) + offset, remaining);

         // None retry errors
        if (bytesRead < 0 && !isRetryErrorNo(errno))
        {
            LOG_err << "Failed to read, errno: " << errno;
            return error_code{errno, system_category()};
        }

        // Retry errors
        if (bytesRead < 0 && isRetryErrorNo(errno))
        {
            continue;
        }

        // End of file and not read all needed
        if (bytesRead == 0 && offset < n)
        {
            LOG_err << "Failed to read, aborted";
            return error_code{ECONNABORTED, system_category()};
        }

        // Success
        assert(bytesRead >= 0);
        offset += static_cast<size_t>(bytesRead);
    }

    // Success
    return error_code{};
}

std::pair<error_code, int>  SocketUtils::connect(const fs::path& socketPath)
{
    // Extra 1 for null terminated
    if (socketPath.native().size() + 1 > maxSocketPathLength())
    {
        LOG_err << "Unix domain socket name is too long, " << socketPath.string();
        return {error_code{ENAMETOOLONG, system_category()}, -1};
    }

    auto fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_err << "Failed to create a UNIX domain socket: " << socketPath.string() << " errno: " << errno;
        return {error_code{errno, system_category()}, -1};
    }

    auto addr = initSocketAddr(socketPath.native());

    if (::connect(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        LOG_err << "Failed to connect " << socketPath.string() << " errno: " << errno;
        ::close(fd);
        return {error_code{errno, system_category()}, -1};
    }

    return {error_code{}, fd};
}

std::pair<error_code, int> SocketUtils::listen(const fs::path& socketPath)
{
    // Try to remove, it may not exist
    removePath(socketPath);

    // Try to create path, it may already exist
    createDirectories(socketPath.parent_path());

    const auto socketPathStr = socketPath.string();

    // Create a UNIX domain socket
    const auto fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        LOG_err << "Failed to create a UNIX domain socket: " << socketPathStr << " errno: " << errno;
        return {error_code{errno, system_category()}, -1};
    }

    // Bind and Listen on
    if (const auto error_code = doBindAndListen(fd, socketPathStr))
    {
        ::close(fd);
        return { error_code, -1};
    }

    // Success
    LOG_verbose << "Listening on UNIX domain socket name: " << socketPathStr;

    return {error_code{}, fd};
}

} // namespace
}
