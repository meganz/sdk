#include "mega/logging.h"
#include "mega/utils.h"
#include "gfxworker/commands.h"
#include "gfxworker/comms.h"
#include "gfxworker/comms_win32.h"
#include "gfxworker/utils.h"
#include <windows.h>

namespace gfx {
namespace comms {

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

bool Win32NamedPipeEndpoint::do_write(void* data, size_t n, DWORD milliseconds)
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
        mPipeHandle,                // pipe handle
        data,                   // message
        static_cast<DWORD>(n),  // message length
        &written,               // bytes written
        overlap.data());                  // not overlapped

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

    // IO PENDING, wait for completion
    LOG_verbose << mName << ": Written " << n << "/" << written << "/" << numberOfBytesTransferred;

    return true;
}

bool Win32NamedPipeEndpoint::do_read(void* out, size_t n, DWORD milliseconds)
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
        overlap.data());        // not overlapped

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

HANDLE  WinGfxCommunicationsClient::connect(LPCTSTR pipeName)
{
    //int retried = 0;
	HANDLE hPipe = INVALID_HANDLE_VALUE;
	while (1)
	{
		hPipe = CreateFile(
			pipeName,       // pipe name
			GENERIC_READ |  // read and write access
			GENERIC_WRITE,
			0,              // no sharing
			NULL,           // default security attributes
			OPEN_EXISTING,  // opens existing pipe
			FILE_FLAG_OVERLAPPED,   // flag and attributes
			NULL);          // no template file

	    // Break if the pipe handle is valid.

		if (hPipe != INVALID_HANDLE_VALUE)
			break;

        // server might not ready and retry
        //if (GetLastError() == ERROR_FILE_NOT_FOUND && retried < 10)
        //{
        //    std::this_thread::sleep_for(std::chrono::milliseconds(200));
        //    continue;
        //}

		// Exit if an error other than ERROR_PIPE_BUSY occurs.

		if (GetLastError() != ERROR_PIPE_BUSY)
		{
			LOG_err << "Could not open pipe. Error Code=" << GetLastError() << " " << mega::winErrorMessage(GetLastError());
			break;
		}

		// All pipe instances are busy, so wait for 10 seconds.

		if (!WaitNamedPipe(pipeName, 10000))
		{
			LOG_warn << "Could not open pipe: 10 second wait timed out.";
			break;
		}
	}

	LOG_verbose << "Connected Handle:" << hPipe;

	return hPipe;
}

bool WinGfxCommunicationsClient::initialize()
{
    HANDLE hPipe = connect(TEXT("\\\\.\\pipe\\mynamedpipe"));

    bool connected = hPipe != INVALID_HANDLE_VALUE;
    if (connected)
    {
        if (mOnConnected)
        {
            mOnConnected(mega::make_unique<Win32NamedPipeEndpointClient>(hPipe, "client"));
        }
    }

    return connected;
}

Win32NamedPipeEndpointServer::~Win32NamedPipeEndpointServer()
{
    if (isValid())
    {
        LOG_verbose << mName << "Endpoint server flush";
        FlushFileBuffers(mPipeHandle);
        LOG_verbose << mName << "Endpoint server disconnect";
        DisconnectNamedPipe(mPipeHandle);
    }
}

std::unique_ptr<gfx::comms::IEndpoint> WinGfxCommunicationsClient::connect()
{
    HANDLE hPipe = connect(TEXT("\\\\.\\pipe\\mynamedpipe"));

    return hPipe == INVALID_HANDLE_VALUE ? nullptr : mega::make_unique<Win32NamedPipeEndpointClient>(hPipe, "client");
}

bool WinGfxCommunicationsServer::initialize()
{
    serverListeningLoop();
    return false;
}

void WinGfxCommunicationsServer::shutdown()
{
    return;
}

bool WinGfxCommunicationsServer::waitForClient(HANDLE hPipe, OVERLAPPED* overlap)
{
    assert(hPipe != INVALID_HANDLE_VALUE);
    assert(overlap);

    // Wait for the client to connect; if it succeeds,
    // the function returns a nonzero value. If the function
    // returns zero, GetLastError returns ERROR_PIPE_CONNECTED.
    bool success = ConnectNamedPipe(hPipe, overlap);
    if (success)
    {
        LOG_verbose << "Client connected";
        return true;
    }

    if (!success && GetLastError() != ERROR_IO_PENDING)
    {
        LOG_verbose << "Client couldn't connect, error=" << GetLastError() << " " << mega::winErrorMessage(GetLastError());
        return false;
    }

    // IO_PENDING
    DWORD numberOfBytesTransferred = 0;
    if (GetOverlappedResultEx(
        hPipe,
        overlap,
        &numberOfBytesTransferred,
        INFINITE, // INFINITE wait
        false))
    {
        LOG_verbose << "Client connected";
        return true;
    }

    LOG_verbose << "Client couldn't connect";
    return false;
}

void WinGfxCommunicationsServer::serverListeningLoop()
{
    WinOverlap overlap;
    if (!overlap.isValid())
    {
        return;
    }
    const DWORD BUFSIZE = 512;
    for (;;)
    {
        LOG_verbose << "server awaiting client connection";

        auto hPipe = CreateNamedPipe(
            L"\\\\.\\pipe\\mynamedpipe",             // pipe name
            PIPE_ACCESS_DUPLEX |      // read/write access
            FILE_FLAG_OVERLAPPED,     // overlapped
            PIPE_TYPE_MESSAGE |       // message type pipe
            PIPE_READMODE_BYTE |      // message-read mode
            PIPE_WAIT,                // blocking mode
            PIPE_UNLIMITED_INSTANCES, // max. instances
            BUFSIZE,                  // output buffer size
            BUFSIZE,                  // input buffer size
            0,                        // client time-out
            NULL);                    // default security attribute

        if (hPipe == INVALID_HANDLE_VALUE)
        {
            LOG_err << "CreateNamedPipe failed, Error=" << GetLastError() << " " << mega::winErrorMessage(GetLastError());
            break;
        }

        bool keepRunning = true;
        if (waitForClient(hPipe, overlap.data())) // connected
        {
            if (mRequestProcessor)
            {
                keepRunning = mRequestProcessor->process(mega::make_unique<Win32NamedPipeEndpointServer>(hPipe, "server"));
            }
        }
        else // not connected
        {
            CloseHandle(hPipe);
        }

        if (!keepRunning)
        {
            LOG_info << "Exiting listening loop";
            break;
        }
    }
}

}
}