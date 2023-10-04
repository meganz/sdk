#include "mega/logging.h"
#include "mega/utils.h"
#include "gfxworker/comms_server_win32.h"
#include "gfxworker/server.h"
#include <windows.h>

namespace mega {
namespace gfx {

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

        std::string pipename = "\\\\.\\pipe\\" + mPipename;
        std::wstring wpipename = std::wstring(pipename.begin(), pipename.end());
        auto hPipe = CreateNamedPipe(
            wpipename.c_str(),             // pipe name
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
