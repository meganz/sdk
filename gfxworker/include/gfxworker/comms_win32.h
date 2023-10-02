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

class Win32NamedPipeEndpointClient : public Win32NamedPipeEndpoint
{
public:
    Win32NamedPipeEndpointClient(HANDLE h, const std::string& name) : Win32NamedPipeEndpoint(h, name) {}

    ~Win32NamedPipeEndpointClient() {}
private:
    Type type() const { return Type::Client; }
};

enum class PosixGfxProtocolVersion
{
    V_1 = 1,
    UNSUPPORTED
};

constexpr const PosixGfxProtocolVersion LATEST_PROTOCOL_VERSION =
        static_cast<PosixGfxProtocolVersion>(static_cast<size_t>(PosixGfxProtocolVersion::UNSUPPORTED) - 1);

using OnClientConnectedFunc = std::function<void(std::unique_ptr<IEndpoint> endpoint)>;

using OnServerConnectedFunc = std::function<bool(std::unique_ptr<IEndpoint> endpoint)>;

class WinGfxCommunicationsClient : public IGfxCommunicationsClient
{
public:
    WinGfxCommunicationsClient(OnClientConnectedFunc onConnected) : mOnConnected(std::move(onConnected)) {}

    bool initialize();

    std::unique_ptr<IEndpoint> connect() override;

private:
    HANDLE connect(LPCTSTR pipeName);

    OnClientConnectedFunc mOnConnected;
};


class WinGfxCommunicationsServer
{
public:
    WinGfxCommunicationsServer(std::unique_ptr<IRequestProcessor> requestProcessor)
        : mRequestProcessor(std::move(requestProcessor))
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
};

} //namespace gfx
} //namespace mega
