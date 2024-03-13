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
        LOG_verbose << "socket " << mName << "_" << mSocket << " closed";
    }
}

bool Socket::doWrite(const void* data, size_t n, TimeoutMs timeout)
{
    const auto errorCode = SocketUtils::write(mSocket, data, n, static_cast<milliseconds>(timeout));
    if (errorCode)
    {
        LOG_err << "Write to socket " << mName << "_" << mSocket << " error: " <<  errorCode.message();
    }

    return !errorCode;
}

bool Socket::doRead(void* out, size_t n, TimeoutMs timeout)
{
    const auto errorCode = SocketUtils::read(mSocket, out, n, static_cast<milliseconds>(timeout));
    if (errorCode)
    {
        LOG_err << "read from socket " << mName << "_" << mSocket << " error: " << errorCode.message();
    }
    return !errorCode;
}

} // end of namespace
}