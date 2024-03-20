#pragma once
#include "mega/gfx/worker/comms.h"

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

private:
    bool doWrite(const void* data, size_t n, TimeoutMs timeout) override;

    bool doRead(void* data, size_t n, TimeoutMs timeout) override;

    // File descriptor to the socket
    int mSocket{-1};

    // A name describes the socket and is used in logs.
    std::string mName;
};

} // end of namespace
}