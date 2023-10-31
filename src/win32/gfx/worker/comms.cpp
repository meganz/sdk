#include "mega/win32/gfx/worker/comms.h"
#include "mega/logging.h"

namespace mega {
namespace gfx {

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

bool Win32NamedPipeEndpoint::do_write(const void* data, size_t n, TimeoutMs timeout)
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

    DWORD written;
    auto success = WriteFile(
        mPipeHandle,            // pipe handle
        data,                   // message
        static_cast<DWORD>(n),  // message length
        &written,               // bytes written
        overlap.data());        // overlapped

    if (success)
    {
        LOG_verbose << mName << ": Written " << n << "/" << written;
        return true;
    }

    // error
    if (!success && GetLastError() != ERROR_IO_PENDING)
    {
        LOG_err << mName << ": WriteFile to pipe failed. error=" << GetLastError() << " " << mega::winErrorMessage(GetLastError());
        return false;
    }

    DWORD milliseconds = static_cast<DWORD>(timeout);
    DWORD numberOfBytesTransferred = 0;
    success = GetOverlappedResultEx(
        mPipeHandle,
        overlap.data(),
        &numberOfBytesTransferred,
        milliseconds,
        false);

    if (!success)
    {
        LOG_err << mName << ": Write to pipe fail to complete error=" << GetLastError() << " " << mega::winErrorMessage(GetLastError());
        return false;
    }

    LOG_verbose << mName << ": Written " << n << "/" << written << "/" << numberOfBytesTransferred;

    return true;
}

bool Win32NamedPipeEndpoint::do_read(void* out, size_t n, TimeoutMs timeout)
{
    WinOverlap overlap;
    if (!overlap.isValid())
    {
        return false;
    }

    // Read from the pipe.
    DWORD cbRead = 0;
    auto success = ReadFile(
        mPipeHandle,            // pipe handle
        out,                    // buffer to receive reply
        static_cast<DWORD>(n),  // size of buffer
        &cbRead,                // number of bytes read
        overlap.data());        // overlapped

    if (success)
    {
        LOG_verbose << mName << ": do_read bytes " << cbRead;
        return true;
    }

    // error
    if (!success && GetLastError() != ERROR_IO_PENDING)
    {
        LOG_err << mName << ": read from pipe failed. error=" << GetLastError() << " " << mega::winErrorMessage(GetLastError());
        return false;
    }

    DWORD milliseconds = static_cast<DWORD>(timeout);
    DWORD numberOfBytesTransferred = 0;
    success = GetOverlappedResultEx(
        mPipeHandle,
        overlap.data(),
        &numberOfBytesTransferred,
        milliseconds,
        false);

    if (!success)
    {
        LOG_err << mName << ": read from pipe fail to complete error=" << GetLastError() << " " << mega::winErrorMessage(GetLastError());
        return false;
    }

    // IO PENDING, wait for completion
    LOG_verbose << mName << ": read " << n << "/" << cbRead << "/" << numberOfBytesTransferred;

    return true;
}

} // end of namespace
}