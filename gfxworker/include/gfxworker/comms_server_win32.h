#pragma once

#include "mega/win32/gfx/worker/comms.h"
#include <minwindef.h>
#include <winbase.h>
#include <windows.h>
#include <thread>
#include <system_error>

namespace mega {
namespace gfx {

class IRequestProcessor;

class Win32NamedPipeEndpointServer : public Win32NamedPipeEndpoint
{
public:
    Win32NamedPipeEndpointServer(HANDLE h, const std::string& name) : Win32NamedPipeEndpoint(h, name) {}

    ~Win32NamedPipeEndpointServer();
private:
    Type type() const { return Type::Server; }
};

enum class PosixGfxProtocolVersion
{
    V_1 = 1,
    UNSUPPORTED
};

constexpr const PosixGfxProtocolVersion LATEST_PROTOCOL_VERSION =
        static_cast<PosixGfxProtocolVersion>(static_cast<size_t>(PosixGfxProtocolVersion::UNSUPPORTED) - 1);

using OnServerConnectedFunc = std::function<bool(std::unique_ptr<IEndpoint> endpoint)>;

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
    WinGfxCommunicationsServer(std::unique_ptr<IRequestProcessor> requestProcessor, const std::string& pipename = "mega_gfxworker", unsigned short aliveSeconds = 60)
        : mRequestProcessor(std::move(requestProcessor))
        , mPipename(pipename)
    {
        mWaitMs = aliveSeconds == 0 ? INFINITE : static_cast<DWORD>(aliveSeconds * 1000);
    }

    bool initialize();
    void shutdown();
private:
    static std::error_code OK;

    void serverListeningLoop();
    std::error_code waitForClient(HANDLE hPipe, OVERLAPPED* overlap);
    OnServerConnectedFunc mOnConnected;
    std::unique_ptr<IRequestProcessor> mRequestProcessor;
    std::unique_ptr<std::thread> mListeningThread;
    std::string mPipename;
    DWORD       mWaitMs;
};

} //namespace gfx
} //namespace mega
