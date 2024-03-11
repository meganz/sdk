#pragma once

#include "mega/posix/gfx/worker/comms.h"

#include <chrono>
#include <memory>
#include <system_error>
#include <string>

namespace mega {
namespace gfx {

class RequestProcessor;

class ServerPosix
{
public:

    /**
     * @brief A server listening on the named pipe for alive seconds
     *
     * @param requestProcessor the request processor
     * @param name the name of the pipe
     * @param aliveSeconds keep alive if the sever hasn't receive any request for
     *                     the given seconds. 0 mean keeping infinitely running even
     *                     if there is no request coming.
     */
    ServerPosix(std::unique_ptr<RequestProcessor> requestProcessor,
                                 const std::string& name = "mega_gfxworker",
                                 unsigned short aliveSeconds = 60);

    void operator()();
private:
    const static std::error_code OK;

    void serverListeningLoop();

    static std::unique_ptr<Socket> listen(const std::string& name);

    std::unique_ptr<RequestProcessor> mRequestProcessor;

    std::string mName;

    static const int MAX_QUEUE_LEN;

    const std::chrono::milliseconds mWaitMs{60000};
};

} //namespace gfx
} //namespace mega
