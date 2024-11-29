#include "mega/logging.h"
#include "mega/win32/gfx/worker/comms_client.h"
#include "mega/gfx/worker/comms.h"
#include "mega/win32/gfx/worker/comms.h"

namespace mega {
namespace gfx {

class ClientNamedPipe : public NamedPipe
{
public:
    ClientNamedPipe(HANDLE h) : NamedPipe(h, "client") {}

private:
    Type type() const { return Type::Client; }
};

GfxCommunicationsClient::GfxCommunicationsClient(const std::string& pipeName)
    : mPipeName(pipeName)
{
}

std::pair<CommError, HANDLE> GfxCommunicationsClient::doConnect(LPCTSTR pipeName)
{
    CommError error = CommError::ERR;
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    while (1)
    {
        hPipe = CreateFile(
            pipeName,       // pipe name
            GENERIC_READ |  // read and write access
            GENERIC_WRITE,
            0,              // no sharing
            NULL,           // default security attributes
            OPEN_EXISTING,  // opens existing pipe
            FILE_FLAG_OVERLAPPED,   // flag and attributes
            NULL);          // no template file

        // Break if the pipe handle is valid.
        if (hPipe != INVALID_HANDLE_VALUE)
        {
            error = CommError::OK;
            break;
        }

        // Exit if an error other than ERROR_PIPE_BUSY occurs.
        auto lastError = GetLastError();
        if (lastError != ERROR_PIPE_BUSY)
        {
            LOG_err << "Could not open pipe. Error Code=" << lastError << " " << mega::winErrorMessage(lastError);
            error = toCommError(lastError);
            break;
        }

        // All pipe instances are busy, so wait for 10 seconds.
        if (!WaitNamedPipe(pipeName, 10000))
        {
            LOG_warn << "Could not open pipe: 10 second wait timed out.";
            error = CommError::TIMEOUT;
            break;
        }
    }

    return {error, hPipe};
}

std::pair<CommError, std::unique_ptr<IEndpoint>> GfxCommunicationsClient::connect()
{
    const auto fullPipeName = win_utils::toFullPipeName(mPipeName);

    auto [error, hPipe] = doConnect(fullPipeName.c_str());

    // Error
    if (error != CommError::OK)
    {
        return {error, nullptr};
    }

    // Success
    assert(hPipe != INVALID_HANDLE_VALUE);
    return {CommError::OK, std::make_unique<ClientNamedPipe>(hPipe)};
}

CommError GfxCommunicationsClient::toCommError(DWORD winError) const
{
    switch (winError) {
    case ERROR_SUCCESS:
        return CommError::OK;
    case ERROR_FILE_NOT_FOUND:
        return CommError::NOT_EXIST;
    default:
        return CommError::ERR;
    }
}

}
}
