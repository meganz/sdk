#pragma once

#include "mega/types.h"
#include "mega/win32/gfx/worker/comms.h"

#include <system_error>

namespace mega {
namespace gfx {

class RequestProcessor;

class Win32NamedPipeEndpointServer : public Win32NamedPipeEndpoint
{
public:
    Win32NamedPipeEndpointServer(HANDLE h, const std::string& name) : Win32NamedPipeEndpoint(h, name) {}

    ~Win32NamedPipeEndpointServer();
private:
    Type type() const { return Type::Server; }
};

class ServerWin32
{
public:

    /**
     * @brief A server listening on the named pipe for alive seconds
     *
     * @param requestProcessor the request processor
     * @param pipeName the name of the pipe
     * @param keepAliveInSeconds keep alive if the sever hasn't receive any request for
     *                           the given seconds. 0 mean keeping infinitely running even
     *                           if there is no request coming.
     */
    ServerWin32(
        std::unique_ptr<RequestProcessor> requestProcessor,
        const std::string& pipeName = "mega_gfxworker",
        unsigned short keepAliveInSeconds = 60)
        : mRequestProcessor(std::move(requestProcessor))
        , mPipeName(pipeName)
    {
        // wait for client to connect timeout is set accordingly
        mWaitMs = keepAliveInSeconds == 0 ? INFINITE : static_cast<DWORD>(keepAliveInSeconds * 1000);
    }

    void operator()();
private:
    const static std::error_code OK;

    void serverListeningLoop();

    std::error_code waitForClient(HANDLE hPipe, OVERLAPPED* overlap);

    std::unique_ptr<RequestProcessor> mRequestProcessor;

    std::string mPipeName;

    DWORD       mWaitMs = INFINITE;
};

} //namespace gfx
} //namespace mega
