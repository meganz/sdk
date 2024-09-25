#include "mega/win32/gfx/worker/comms.h"
#include "mega/logging.h"
#include "mega/filesystem.h"

using std::chrono::milliseconds;

namespace mega
{
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

WinOverlapped::WinOverlapped()
{
    mOverlapped.Offset = 0;
    mOverlapped.OffsetHigh = 0;
    mOverlapped.hEvent = CreateEvent(
        NULL,    // default security attribute
        TRUE,    // manual-reset event
        TRUE,    // initial state = signaled
        NULL);   // unnamed event object

    if (mOverlapped.hEvent == NULL)
    {
        LOG_err << "CreateEvent failed. error code=" << GetLastError() << " " << mega::winErrorMessage(GetLastError());
    }
}

WinOverlapped::~WinOverlapped()
{
    if (!mOverlapped.hEvent)
    {
        CloseHandle(mOverlapped.hEvent);
    }
}

OVERLAPPED* WinOverlapped::data()
{
    return &mOverlapped;
}

NamedPipe::NamedPipe(NamedPipe&& other)
{
    this->mPipeHandle = other.mPipeHandle;
    this->mName = std::move(other.mName);
    other.mPipeHandle = INVALID_HANDLE_VALUE;
}

NamedPipe::~NamedPipe()
{
    if (mPipeHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(mPipeHandle);
        LOG_verbose << "endpoint " << mName << "_" << mPipeHandle << " closed";
    }
}

bool NamedPipe::doWrite(const void* data, size_t n, milliseconds timeout)
{
    auto writeOverlappedFn = [this, n, data](OVERLAPPED* overlapped){
        DWORD written;
        auto result = WriteFile(
            mPipeHandle,            // pipe handle
            data,                   // message
            static_cast<DWORD>(n),  // message length
            &written,               // bytes written
            overlapped);               // overlapped

        return result;
    };

    return doOverlappedOperation(writeOverlappedFn, timeout, "write");
}

bool NamedPipe::doRead(void* out, size_t n, milliseconds timeout)
{
    auto readOverlappedFn = [this, n, out](OVERLAPPED* overlapped){
        DWORD cbRead = 0;
        bool result = ReadFile(
            mPipeHandle,            // pipe handle
            out,                    // buffer to receive reply
            static_cast<DWORD>(n),  // size of buffer
            &cbRead,                // number of bytes read
            overlapped);               // overlapped

        return result;
    };

    return doOverlappedOperation(readOverlappedFn, timeout, "read");
}

bool NamedPipe::doOverlappedOperation(std::function<bool(OVERLAPPED*)> op,
                                      milliseconds timeout,
                                      const std::string& opStr)
{
    if (mPipeHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    WinOverlapped overlapped;
    if (!overlapped.isValid())
    {
        return false;
    }

    // Call Op.
    if (op(overlapped.data()))
    {
        return true;
    }

    // Error
    auto lastError = GetLastError();
    if (lastError!= ERROR_IO_PENDING)
    {
        LOG_err << mName << ": " << opStr << " pipe failed. error=" << lastError << " " << mega::winErrorMessage(lastError);
        return false;
    }

    // Wait op to complete. Negative timeout is infinite
    DWORD waitTimeout = timeout.count() < 0 ? INFINITE : static_cast<DWORD>(timeout.count());
    DWORD numberOfBytesTransferred = 0;
    bool success = GetOverlappedResultEx(mPipeHandle,
                                         overlapped.data(),
                                         &numberOfBytesTransferred,
                                         waitTimeout,
                                         false);

    if (!success)
    {
        lastError = GetLastError();
        LOG_err << mName << ": " << opStr << " pipe fail to complete error=" << lastError << " " << mega::winErrorMessage(lastError);
        return false;
    }

    // IO completed
    return true;
}

} // namespace
}
