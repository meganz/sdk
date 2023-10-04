#pragma once

#include "mega/win32/gfx/worker/comms.h"
#include <windows.h>
#include <thread>

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
    WinGfxCommunicationsServer(std::unique_ptr<IRequestProcessor> requestProcessor, const std::string& pipename = "mega_gfxworker")
        : mRequestProcessor(std::move(requestProcessor))
        , mPipename(pipename)
    {

    }

    bool initialize();
    void shutdown();
private:
    void serverListeningLoop();
    bool waitForClient(HANDLE hPipe, OVERLAPPED* overlap);
    OnServerConnectedFunc mOnConnected;
    std::unique_ptr<IRequestProcessor> mRequestProcessor;
    std::unique_ptr<std::thread> mListeningThread;
    std::string mPipename;
};

} //namespace gfx
} //namespace mega
