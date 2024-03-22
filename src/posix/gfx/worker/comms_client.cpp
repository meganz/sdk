#include "mega/posix/gfx/worker/comms_client.h"
#include "mega/posix/gfx/worker/socket_utils.h"
#include "mega/posix/gfx/worker/comms.h"

namespace mega {
namespace gfx {

GfxCommunicationsClient::GfxCommunicationsClient(const std::string& socketName)
    : mSocketName(socketName)
{
}

std::pair<CommError, std::unique_ptr<IEndpoint>> GfxCommunicationsClient::connect()
{
    auto [errorCode, fd] = SocketUtils::connect(SocketUtils::toSocketPath(mSocketName));
    if (errorCode)
    {
        return {toCommError(errorCode.value()), nullptr};
    }

    std::unique_ptr<IEndpoint> endpoint = std::make_unique<Socket>(fd, "client");
    return {CommError::OK, std::move(endpoint)};
}

CommError GfxCommunicationsClient::toCommError(int error) const
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