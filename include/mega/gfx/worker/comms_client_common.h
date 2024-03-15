#pragma once

#include "mega/gfx/worker/comms.h"
namespace mega {
namespace gfx {

class IGfxCommunicationsClient
{
public:
    virtual ~IGfxCommunicationsClient() = default;

    virtual CommError connect(std::unique_ptr<IEndpoint>& endpoint) = 0;
};

}
}
