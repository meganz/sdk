#include "posix/server.h"
#include "processor.h"

#include "mega/logging.h"
#include <sys/socket.h>
#include <sys/un.h>

namespace mega {
namespace gfx {

const int ServerPosix::MAX_QUEUE_LEN = 10;

ServerPosix::ServerPosix(std::unique_ptr<RequestProcessor> requestProcessor,
                                                           const std::string& name,
                                                           unsigned short aliveSeconds)
    : mRequestProcessor(std::move(requestProcessor))
    , mName(name)
{
        // wait for client to connect timeout is set accordingly
        // mWaitMs = aliveSeconds == 0 ? INFINITE : static_cast<DWORD>(aliveSeconds * 1000);
}

void ServerPosix::operator()()
{

}


int ServerPosix::listen(const std::string& name)
{
    struct sockaddr_un un;

    // check name
    // extra 1 for null terminated
    size_t max_size = sizeof(un.sun_path) - 1;
    if (name.size() >= max_size)
    {
        LOG_err << "unix domain socket name is too long, " << name;
        return -1;
    }

    // create a UNIX domain socket
    int fd = -1;
    if ((fd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
    {
        LOG_err << "fail to create a UNIX domain socket: " << name << " errno: " << errno;
        return -2;
    }

    // the name might exists due to crash
    // another possiblity is a server with same name already exists
    // fail to unlink is not an error: such as not exists as for most cases
    if (unlink(name.c_str()) < 0)
    {
        LOG_info << "fail to unlink: " << name << " errno: " << errno;
    }

    // fill address
    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    strncpy(un.sun_path, name.c_str(), max_size);

    // bind name
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&un), sizeof(un)) == -1)
    {
        LOG_err << "fail to bind UNIX domain socket name: " << name << " errno: " << errno;
        close(fd);
        return -3;
    }

    // listen
    if (::listen(fd, MAX_QUEUE_LEN) < 0)
    {
        LOG_err << "fail to listen UNIX domain socket name: " << name << " errno: " << errno;
        close(fd);
        return -4;
    }

    LOG_verbose << "listening on UNIX domain socket name: " << name;

    return fd;
}


}
}
