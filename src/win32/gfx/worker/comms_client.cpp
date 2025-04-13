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
    constexpr DWORD waitTimeoutMs{3000};
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
        if (const auto lastError = GetLastError(); lastError != ERROR_PIPE_BUSY)
        {
            LOG_err << "Could not open pipe. Error Code=" << lastError << " " << mega::winErrorMessage(lastError);
            error = toCommError(lastError);
            break;
        }

        // Server is busy. Wait and get one instance. Continue to connect
        if (WaitNamedPipe(pipeName, waitTimeoutMs))
            continue;

        if (const auto lastError = GetLastError(); lastError == ERROR_SEM_TIMEOUT)
        {
            LOG_warn << "WaitNamedPipe: timed out.";
            error = CommError::TIMEOUT;
            break;
        }
        else
        {
            LOG_err << "WaitNamedPipe Error Code=" << lastError << " "
                    << mega::winErrorMessage(lastError);
            error = toCommError(lastError);
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
    case ERROR_BROKEN_PIPE: // Usually mean the other side closed the handle
        return CommError::CLOSED;
    default:
        return CommError::ERR;
    }
}

}
}
