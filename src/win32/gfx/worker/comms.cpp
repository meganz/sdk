#include "mega/win32/gfx/worker/comms.h"
#include "mega/logging.h"
#include "mega/filesystem.h"

namespace mega {
namespace gfx {
namespace win_utils
{

std::wstring toFullPipeName(const std::string& name)
{
    const std::string pipeName = "\\\\.\\pipe\\" + name;
    std::wstring pipeNameW;
    LocalPath::path2local(&pipeName, &pipeNameW);
    return pipeNameW;
}

}

WinOverlap::WinOverlap()
{
    mOverlap.Offset = 0;
    mOverlap.OffsetHigh = 0;
    mOverlap.hEvent = CreateEvent(
        NULL,    // default security attribute
        TRUE,    // manual-reset event
        TRUE,    // initial state = signaled
        NULL);   // unnamed event object

    if (mOverlap.hEvent == NULL)
    {
        LOG_err << "CreateEvent failed. error code=" << GetLastError() << " " << mega::winErrorMessage(GetLastError());
    }
}

WinOverlap::~WinOverlap()
{
    if (!mOverlap.hEvent)
    {
        CloseHandle(mOverlap.hEvent);
    }
}

OVERLAPPED* WinOverlap::data()
{
    return &mOverlap;
}

Win32NamedPipeEndpoint::Win32NamedPipeEndpoint(Win32NamedPipeEndpoint&& other)
{
    this->mPipeHandle = other.mPipeHandle;
    this->mName = std::move(other.mName);
    other.mPipeHandle = INVALID_HANDLE_VALUE;
}

Win32NamedPipeEndpoint::~Win32NamedPipeEndpoint()
{
    if (mPipeHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(mPipeHandle);
        LOG_verbose << "endpoint " << mName << "_" << mPipeHandle << " closed";
    }
}

bool Win32NamedPipeEndpoint::doWrite(const void* data, size_t n, TimeoutMs timeout)
{
    auto writeOp = [this, n, data](OVERLAPPED* overlap){
        DWORD written;
        auto result = WriteFile(
            mPipeHandle,            // pipe handle
            data,                   // message
            static_cast<DWORD>(n),  // message length
            &written,               // bytes written
            overlap);               // overlapped
        //if (result) LOG_verbose << "write " << written << " bytes OK";
        return result;

    };

    return doOverlapOp(writeOp, timeout, "write");
}

bool Win32NamedPipeEndpoint::doRead(void* out, size_t n, TimeoutMs timeout)
{
    auto readOp = [this, n, out](OVERLAPPED* overlap){
        DWORD cbRead = 0;
        bool result = ReadFile(
            mPipeHandle,            // pipe handle
            out,                    // buffer to receive reply
            static_cast<DWORD>(n),  // size of buffer
            &cbRead,                // number of bytes read
            overlap);               // overlapped
        //if (result) LOG_verbose << "read " << cbRead << " bytes OK";
        return result;
    };

    return doOverlapOp(readOp, timeout, "read");
}

bool Win32NamedPipeEndpoint::doOverlapOp(std::function<bool(OVERLAPPED*)>op,
                                          TimeoutMs timeout,
                                          const std::string& opStr)
{
    if (mPipeHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    WinOverlap overlap;
    if (!overlap.isValid())
    {
        return false;
    }

    // call Op.
    if (op(overlap.data()))
    {
        return true;
    }

    // error
    auto lastError = GetLastError();
    if (lastError!= ERROR_IO_PENDING)
    {
        LOG_err << mName << ": " << opStr << " pipe failed. error=" << lastError << " " << mega::winErrorMessage(lastError);
        return false;
    }

    // wait op to complete
    DWORD milliseconds = static_cast<DWORD>(timeout);
    DWORD numberOfBytesTransferred = 0;
    bool success = GetOverlappedResultEx(
        mPipeHandle,
        overlap.data(),
        &numberOfBytesTransferred,
        milliseconds,
        false);

    if (!success)
    {
        lastError = GetLastError();
        LOG_err << mName << ": " << opStr << " pipe fail to complete error=" << lastError << " " << mega::winErrorMessage(lastError);
        return false;
    }

    // IO completed
    // LOG_verbose << mName << ": " << opStr << " complete bytes transferred: " << numberOfBytesTransferred;
    return true;
}

} // end of namespace
}