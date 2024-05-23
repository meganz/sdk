#pragma once

#include <chrono>
#include <memory>
#include <string>

namespace mega {
namespace gfx {

class RequestProcessor;

class ServerPosix
{
public:

    /**
     * @brief A server listening on the named unix domain socket for alive seconds
     *
     * @param requestProcessor the request processor
     * @param socketName the socket name
     * @param aliveSeconds keep alive if the sever hasn't receive any request for
     *                     the given seconds. 0 mean keeping infinitely running even
     *                     if there is no request coming.
     */
    ServerPosix(std::unique_ptr<RequestProcessor> requestProcessor,
                const std::string& socketName = "mega_gfxworker",
                unsigned short aliveSeconds = 60);

    void operator()();
private:

    void serverListeningLoop();

    std::unique_ptr<RequestProcessor> mRequestProcessor;

    std::string mSocketName;

    const std::chrono::milliseconds mWaitMs{60000};
};

} // namespace
}
