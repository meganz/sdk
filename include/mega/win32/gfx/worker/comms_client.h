#pragma once

#include "mega/win32/gfx/worker/comms.h"
#include <windows.h>

namespace mega {
namespace gfx {

class Win32NamedPipeEndpointClient : public Win32NamedPipeEndpoint
{
public:
    Win32NamedPipeEndpointClient(HANDLE h, const std::string& name) : Win32NamedPipeEndpoint(h, name) {}

    ~Win32NamedPipeEndpointClient() {}
private:
    Type type() const { return Type::Client; }
};

using OnClientConnectedFunc = std::function<void(std::unique_ptr<IEndpoint> endpoint)>;

class WinGfxCommunicationsClient : public IGfxCommunicationsClient
{
public:
    WinGfxCommunicationsClient(OnClientConnectedFunc onConnected, const std::string& pipename = "mega_gfxworker")
        : mOnConnected(std::move(onConnected))
        , mPipename(pipename)
    {

    }

    std::unique_ptr<IEndpoint> connect() override;

private:
    HANDLE connect(LPCTSTR pipeName);

    OnClientConnectedFunc mOnConnected;

    std::string mPipename;
};

} // end of namespace
}