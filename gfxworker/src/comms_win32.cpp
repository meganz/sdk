#include "mega/logging.h"
#include "mega/utils.h"
#include "mega/gfx/worker/comms.h"
#include "gfxworker/comms_win32.h"
#include "gfxworker/server.h"
#include <windows.h>

namespace mega {
namespace gfx {

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

std::unique_ptr<IEndpoint> WinGfxCommunicationsClient::connect()
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
