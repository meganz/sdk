#include "gfx/worker/comms.h"

#include "mega/logging.h"
#include "mega/posix/gfx/worker/comms_client.h"
#include "mega/posix/gfx/worker/socket_utils.h"
#include "mega/gfx/worker/comms.h"

#include <memory>

namespace mega {
namespace gfx {

CommError PosixGfxCommunicationsClient::connect(std::unique_ptr<IEndpoint>& endpoint)
{
    auto socket = std::make_unique<Socket>(::socket(AF_UNIX, SOCK_STREAM, 0), "client");
    if (!socket->isValid()) {
        LOG_err << "socket error: " << errno;
        return toCommError(errno);
    }

    auto socketPath = SocketUtils::toSocketPath(mName);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(socket->fd(), (const struct sockaddr *) &addr, sizeof(addr)) == -1)
    {
        LOG_err << "connect error: " << errno;
        return toCommError(errno);
    }

    endpoint = std::move(socket);
    return CommError::OK;
}

CommError PosixGfxCommunicationsClient::toCommError(int error) const
{
    switch (error) {
        case ENOENT:        // case socket hasn't been created yet
        case ECONNREFUSED:  // case socket is created, but server is not listenning
        {
            return CommError::NOT_EXIST;
        }
        default:
        {
            return CommError::ERR;
        }
    }
}

}
}