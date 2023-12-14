#pragma once
#include "mega/gfx/worker/comms.h"

#include <windows.h>

namespace mega {
namespace gfx {
namespace win_utils
{

std::wstring toFullPipeName(const std::string& name);

}

class Win32NamedPipeEndpoint : public IEndpoint
{
public:
    Win32NamedPipeEndpoint(HANDLE h, const std::string& name) : mPipeHandle(h), mName(name) {}

    Win32NamedPipeEndpoint(const Win32NamedPipeEndpoint&) = delete;

    Win32NamedPipeEndpoint(Win32NamedPipeEndpoint&& other);

    ~Win32NamedPipeEndpoint();

    bool isValid() const { return mPipeHandle != INVALID_HANDLE_VALUE; }
protected:
    enum class Type
    {
        Client,
        Server
    };

    HANDLE mPipeHandle;

    std::string mName;

private:
    bool doWrite(const void* data, size_t n, TimeoutMs timeout) override;

    bool doRead(void* data, size_t n, TimeoutMs timeout) override;

    bool doOverlapOp(std::function<bool(OVERLAPPED*)>op,
                      TimeoutMs timeout,
                      const std::string& opStr);

    virtual Type type() const = 0;
};

class WinOverlap final
{
public:
    WinOverlap();
    ~WinOverlap();

    OVERLAPPED* data();

    bool isValid() const { return mOverlap.hEvent != NULL; };

private:
    OVERLAPPED mOverlap;
};

} // end of namespace
}