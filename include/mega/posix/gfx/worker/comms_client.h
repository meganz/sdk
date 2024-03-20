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

    CommError connect(std::unique_ptr<IEndpoint>& endpoint) override;

private:

    CommError toCommError(int error) const;

    std::string mSocketName;
};

} // end of namespace
}