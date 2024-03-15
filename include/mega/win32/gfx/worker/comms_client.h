#pragma once

#include "mega/gfx/worker/comms_client_common.h"
#include <windows.h>

namespace mega {
namespace gfx {

class WinGfxCommunicationsClient : public IGfxCommunicationsClient
{
public:
    WinGfxCommunicationsClient(const std::string& pipeName)
        : mPipeName(pipeName)
    {

    }

    CommError connect(std::unique_ptr<IEndpoint>& endpoint) override;

private:
    CommError doConnect(LPCTSTR pipeName, HANDLE &hPipe);

    CommError toCommError(DWORD winError) const;

    std::string mPipeName;
};

} // end of namespace
}