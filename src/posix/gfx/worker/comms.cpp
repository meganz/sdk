#include "mega/posix/gfx/worker/comms.h"
#include "mega/logging.h"
#include "mega/clock.h"

#include <poll.h>

#include <chrono>
#include <system_error>

using std::chrono::milliseconds;

namespace mega {
namespace gfx {
namespace posix_utils
{
using std::chrono::duration_cast;
using std::error_code;
using std::system_category;

bool isPollError(int event)
{
    return event & (POLLERR | POLLHUP | POLLNVAL);
}

bool isRetryErrorNo(int errorNo)
{
    return errorNo == EAGAIN || errorNo == EWOULDBLOCK || errorNo == EINTR;
}

error_code poll(std::vector<struct pollfd> fds, milliseconds timeout)
{
    // Remaining timeout in case of EINTR
    const ScopedSteadyClock clock;
    milliseconds remaining{timeout};

    int ret = 0;
    do
    {
        ret = ::poll(fds.data(), fds.size(), static_cast<int>(timeout.count()));
        remaining -= duration_cast<milliseconds>(clock.passedTime());
    } while (ret < 0 && errno == EINTR && remaining > milliseconds{0});

    if (ret < 0)
    {
        LOG_err << "Error in poll: " << errno;
        return error_code{errno, system_category()};
    }
    else if (ret == 0)
    {
        return error_code{ETIMEDOUT, system_category()};
    }

    return error_code{};
}

error_code pollFd(int fd, short events, milliseconds timeout)
{
    // Poll
    std::vector<struct pollfd> fds{
        {.fd = fd, .events = events, .revents = 0}
    };

    if (auto errorCode = poll(fds, timeout); errorCode)
    {
        return errorCode;
    }

    // check if the poll returns an error event
    auto& polledFd = fds[0];
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

error_code write(int fd, const void* data, size_t n, milliseconds timeout)
{
    size_t offset = 0;
    while (offset < n) {
        // Poll
        if (auto errorCode = pollForWrite(fd, timeout); errorCode)
        {
            return errorCode;
        }

        // Write
        size_t remaining = n - offset;
        ssize_t written = ::write(fd, static_cast<const char *>(data) + offset, remaining);
        if (written < 0 && isRetryErrorNo(errno))
        {
            continue;                                    // retry
        }
        else if (written < 0)
        {
            return error_code{errno, system_category()}; // error
        }
        else
        {
            offset += static_cast<size_t>(written);      // success
        }
    }

    return error_code{};
}

//
// Read until count bytes or timeout
//
error_code read(int fd, void* buf, size_t count, milliseconds timeout)
{
    size_t offset = 0;
    while (offset < count) {
        // Poll
        if (auto errorCode = pollForRead(fd, timeout); errorCode)
        {
            return errorCode;
        }

        // Read
        size_t remaining = count - offset;
        ssize_t hasRead = ::read(fd, static_cast<char *>(buf) + offset, remaining);
        if (hasRead < 0 && isRetryErrorNo(errno))
        {
            continue;                                    // Again
        }
        else if (hasRead < 0)
        {
            return error_code{errno, system_category()}; // error
        }
        else
        {
            offset += static_cast<size_t>(hasRead);      // success
        }
    }

    return error_code{};
}

std::pair<error_code, std::unique_ptr<Socket>> accept(int listeningFd, milliseconds timeout)
{
    do {
        auto errorCode = posix_utils::pollForAccept(listeningFd, timeout);
        if (errorCode)
        {
            return {errorCode, nullptr};   // error
        }

        auto dataSocket = std::make_unique<Socket>(::accept(listeningFd, nullptr, nullptr), "server");
        if (!dataSocket->isValid() && isRetryErrorNo(errno))
        {
            LOG_err << "Fail to accept: " << errno;     // retry
            continue;
        }
        else if (!dataSocket->isValid())
        {
            return {error_code{errno, system_category()}, nullptr}; // error
        }
        else
        {
            return {error_code{}, std::move(dataSocket)};                         // success
        }
    }while (true);
}

}

Socket::Socket(Socket&& other)
{
    this->mSocket = other.mSocket;
    this->mName = std::move(other.mName);
    other.mSocket = -1;
}

Socket::~Socket()
{
    if (isValid())
    {
        ::close(mSocket);
        LOG_verbose << "socket " << mName << "_" << mSocket << " closed";
    }
}

bool Socket::doWrite(const void* data, size_t n, TimeoutMs timeout)
{
    const auto errorCode = posix_utils::write(mSocket, data, n, static_cast<milliseconds>(timeout));
    if (errorCode)
    {
        LOG_err << "Write to socket " << mName << "_" << mSocket << " error: " <<  errorCode.message();
    }

    return !errorCode;
}

bool Socket::doRead(void* out, size_t n, TimeoutMs timeout)
{
    const auto errorCode = posix_utils::read(mSocket, out, n, static_cast<milliseconds>(timeout));
    if (errorCode)
    {
        LOG_err << "read from socket " << mName << "_" << mSocket << " error: " << errorCode.message();
    }
    return !errorCode;
}

} // end of namespace
}