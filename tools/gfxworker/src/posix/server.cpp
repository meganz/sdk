#include "posix/server.h"
#include "gfx/worker/comms.h"
#include "processor.h"

#include "mega/logging.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

#include <memory>
#include <system_error>
#include <vector>

namespace mega {
namespace gfx {

const int ServerPosix::MAX_QUEUE_LEN = 10;

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
    auto socket = listen(mName);

    if (!socket || !socket->isValid()) return;
    
    auto socketFd = socket->fd();
    for (;;) 
    {
        auto [errorCode, dataSocket] = posix_utils::accept(socketFd, mWaitMs);
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

std::unique_ptr<Socket> ServerPosix::listen(const std::string& name)
{
    struct sockaddr_un un;

    // check name
    // extra 1 for null terminated
    size_t max_size = sizeof(un.sun_path) - 1;
    if (name.size() >= max_size)
    {
        LOG_err << "unix domain socket name is too long, " << name;
        return nullptr;
    }

    // create a UNIX domain socket
    auto socket = std::make_unique<Socket>(::socket(AF_UNIX, SOCK_STREAM, 0), "server");
    if (!socket->isValid())
    {
        LOG_err << "fail to create a UNIX domain socket: " << name << " errno: " << errno;
        return nullptr;
    }

    // the name might exists due to crash
    // another possiblity is a server with same name already exists
    // fail to unlink is not an error: such as not exists as for most cases
    if (::unlink(name.c_str()) < 0)
    {
        LOG_info << "fail to unlink: " << name << " errno: " << errno;
    }

    // fill address
    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    strncpy(un.sun_path, name.c_str(), max_size);

    // bind name
    if (::bind(socket->fd(), reinterpret_cast<struct sockaddr*>(&un), sizeof(un)) == -1)
    {
        LOG_err << "fail to bind UNIX domain socket name: " << name << " errno: " << errno;
        return nullptr;
    }

    // listen
    if (::listen(socket->fd(), MAX_QUEUE_LEN) < 0)
    {
        LOG_err << "fail to listen UNIX domain socket name: " << name << " errno: " << errno;
        return nullptr;
    }

    LOG_verbose << "listening on UNIX domain socket name: " << name;

    return socket;
}


}
}
