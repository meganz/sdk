#pragma once

#include "mega/gfx/worker/commands.h"
#include "mega/gfx/worker/comms.h"
#include "mega/gfx/worker/tasks.h"
#include "megafs.h"
#include "thread_pool.h"

#include <chrono>
#include <memory>

namespace mega {
namespace gfx {

class GfxProcessor
{
public:
    GfxProcessor();

    GfxTaskResult process(const GfxTask& task);
    bool processMediaInfo(const std::string& path, MediaProperties& mp);

    std::string supportedformats() const;

    std::string supportedvideoformats() const;

private:
    std::unique_ptr<::mega::IGfxProvider> mGfxProvider;
};

class RequestProcessor
{
public:
    RequestProcessor(size_t threadCount = 6, size_t maxQueueSize = 12);

    // process the request. return true if processsing should
    // be stopped such as received a shutdown request
    bool process(std::unique_ptr<IEndpoint> endpoint);

private:
    void processHello(IEndpoint* endpoint);

    void processShutDown(IEndpoint* endpoint);

    void processGfx(IEndpoint* endpoint, CommandNewGfx* request);

    void processSupportFormats(IEndpoint* endpoint);

    void processMediaInfo(IEndpoint* endpoint, CommandMediaInfo* request);

    GfxProcessor mGfxProcessor;

    ThreadPool mThreadPool;

    static constexpr std::chrono::milliseconds READ_TIMEOUT{5000};

    static constexpr std::chrono::milliseconds WRITE_TIMEOUT{5000};
};

} // namespace
}
