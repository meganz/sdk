#include "mega/logging.h"
#include "mega/win32/gfx/worker/comms_client.h"

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

std::unique_ptr<IEndpoint> WinGfxCommunicationsClient::connect()
{
    std::string pipename = "\\\\.\\pipe\\" + mPipename;
    std::wstring wpipename = std::wstring(pipename.begin(), pipename.end());

    HANDLE hPipe = connect(wpipename.c_str());

    return hPipe == INVALID_HANDLE_VALUE ? nullptr : mega::make_unique<Win32NamedPipeEndpointClient>(hPipe, "client");
}

}
}