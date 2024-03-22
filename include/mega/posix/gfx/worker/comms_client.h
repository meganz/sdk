#pragma once

#include "mega/gfx/worker/comms.h"
#include "mega/gfx/worker/comms_client_common.h"

namespace mega {
namespace gfx {

class GfxCommunicationsClient : public IGfxCommunicationsClient
{
public:
    GfxCommunicationsClient(const std::string& socketName)
        : mSocketName(socketName)
    {
    }

    std::pair<CommError, std::unique_ptr<IEndpoint>> connect() override;

private:

    CommError toCommError(int error) const;

    std::string mSocketName;
};

} // namespace
}