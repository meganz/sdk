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

class WinGfxCommunicationsServer
{
public:

    /**
     * @brief A server listening on the named pipe for alive seconds
     *
     * @param requestProcessor the request processor
     * @param pipename the name of the pipe
     * @param aliveSeconds keep alive if the sever hasn't receive any request for
     *                     the given seconds. 0 mean keeping infinitely running even
     *                     if there is no request coming.
     */
    WinGfxCommunicationsServer(std::unique_ptr<RequestProcessor> requestProcessor, const std::string& pipename = "mega_gfxworker", unsigned short aliveSeconds = 60)
        : mRequestProcessor(std::move(requestProcessor))
        , mPipename(pipename)
    {
        // wait for client to connect timeout is set accordingly
        mWaitMs = aliveSeconds == 0 ? INFINITE : static_cast<DWORD>(aliveSeconds * 1000);
    }

    void operator()();
private:
    const static std::error_code OK;

    void serverListeningLoop();

    std::error_code waitForClient(HANDLE hPipe, OVERLAPPED* overlap);

    std::unique_ptr<RequestProcessor> mRequestProcessor;

    std::string mPipename;

    DWORD       mWaitMs;
};

} //namespace gfx
} //namespace mega
