#include "posix/server.h"
#include "processor.h"

#include "mega/logging.h"
#include "mega/gfx/worker/comms.h"
#include "mega/posix/gfx/worker/comms.h"
#include "mega/posix/gfx/worker/socket_utils.h"

#include <memory>

namespace mega {
namespace gfx {

ServerPosix::ServerPosix(std::unique_ptr<RequestProcessor> requestProcessor,
                                                           const std::string& name,
                                                           unsigned short aliveSeconds)
    : mRequestProcessor{std::move(requestProcessor)}
    , mName{name}
    , mWaitMs{ aliveSeconds == 0 ? -1 : aliveSeconds * 1000} // negative is infinitely
{
}

void ServerPosix::operator()()
{
    serverListeningLoop();
}

void ServerPosix::serverListeningLoop()
{
    // Listen
    const auto [ret, listenFd] = SocketUtils::listen(SocketUtils::toSocketPath(mName));
    if (ret)
    {
        LOG_err << "Fail to listen on " << mName << ": " << ret.message();
        return;
    }

    // Take ownership
    const auto listenSocket = std::make_unique<Socket>(listenFd, "listen");

    // Process connections
    for (;;)
    {
        const auto [errorCode, dataFd] = SocketUtils::accept(listenFd, mWaitMs);
        if (errorCode == std::errc::timed_out)
        {
            LOG_info << "Exit listening loop, No more requests.";
            break;
        }
        else if (errorCode)
        {
            LOG_info << "Exit listening loop, Error: " << errorCode.message();
            break;
        }

        // Take ownership
        auto dataSocket = std::make_unique<Socket>(dataFd, "accept");

        // Process requests
        bool stopRunning = false;
        if (mRequestProcessor)
        {
            stopRunning = mRequestProcessor->process(std::move(dataSocket));
        }
        if (stopRunning)
        {
            LOG_info << "Exit listening loop by request";
            break;
        }
    }
}

}
}
