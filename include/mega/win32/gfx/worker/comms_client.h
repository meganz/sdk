#pragma once

#include "mega/gfx/worker/comms_client_common.h"
#include <windows.h>

namespace mega {
namespace gfx {

class GfxCommunicationsClient : public IGfxCommunicationsClient
{
public:
    GfxCommunicationsClient(const std::string& pipeName);

    // Connect to the server
    // On success, a CommError::OK and a valid endpoint pair is returned
    // On failure, a CommError error and nullptr pair is returned
    std::pair<CommError, std::unique_ptr<IEndpoint>> connect() override;

private:
    // Do connection to the named pipe server
    // On success, a CommError::OK and a valid handle pair is returned
    // On failure, a CommError error and INVALID_HANDLE_VALUE pair is returned
    std::pair<CommError, HANDLE> doConnect(LPCTSTR pipeName);

    CommError toCommError(DWORD winError) const;

    std::string mPipeName;
};

} // namespace
}