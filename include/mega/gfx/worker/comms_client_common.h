#pragma once

#include "mega/gfx/worker/comms.h"

#include <utility>

namespace mega {
namespace gfx {

class IGfxCommunicationsClient
{
public:
    virtual ~IGfxCommunicationsClient() = default;

    virtual std::pair<CommError, std::unique_ptr<IEndpoint>> connect() = 0;
};

}
}
