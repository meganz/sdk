#pragma once

#include "mega/gfx/worker/comms.h"
#include "mega/posix/gfx/worker/comms.h"

namespace mega {
namespace gfx {

class PosixGfxCommunicationsClient : public IGfxCommunicationsClient
{
public:
    PosixGfxCommunicationsClient(const std::string& name)
        : mName(name)
    {

    }

    CommError connect(std::unique_ptr<IEndpoint>& endpoint) override;

private:

    CommError toCommError(int error) const;

    std::string mName;
};

} // end of namespace
}