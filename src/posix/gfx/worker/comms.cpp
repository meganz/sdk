#include "mega/posix/gfx/worker/comms.h"
#include "mega/posix/gfx/worker/socket_utils.h"
#include "mega/logging.h"

#include <chrono>

using std::chrono::milliseconds;
namespace mega {
namespace gfx {

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
        LOG_verbose << "Socket " << mName << "_" << mSocket << " closed";
    }
}

bool Socket::doWrite(const void* data, size_t n, milliseconds timeout)
{
    const auto errorCode = SocketUtils::write(mSocket, data, n, timeout);
    if (errorCode)
    {
        LOG_err << "Write to socket " << mName << "_" << mSocket << " error: " <<  errorCode.message();
    }

    return !errorCode;
}

bool Socket::doRead(void* out, size_t n, milliseconds timeout)
{
    const auto errorCode = SocketUtils::read(mSocket, out, n, timeout);
    if (errorCode)
    {
        LOG_err << "Read from socket " << mName << "_" << mSocket << " error: " << errorCode.message();
    }
    return !errorCode;
}

} // namespace
}
