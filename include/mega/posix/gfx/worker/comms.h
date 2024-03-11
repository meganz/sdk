#pragma once
#include "mega/types.h"
#include "mega/gfx/worker/comms.h"

#include <poll.h>

#include <system_error>
#include <chrono>
namespace mega {
namespace gfx {
namespace posix_utils
{

// Check if it contains poll error events
bool isPollError(int event);

// Poll fds with retries on EINTR case
std::error_code poll(std::vector<struct pollfd> fds, std::chrono::milliseconds timeout);

}
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

} // end of namespace
}