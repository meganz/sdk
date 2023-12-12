#pragma once

#include "threadpool.h"

#include "mega/gfx/worker/comms.h"
#include "mega/gfx/worker/commands.h"
#include "mega/gfx/freeimage.h"
#include "mega/gfx/worker/tasks.h"
#include "megafs.h"

#include <memory>
#include <unordered_map>
#include <mutex>

namespace mega {
namespace gfx {

class GfxProcessor
{
public:
    GfxProcessor() = delete;

    GfxProcessor(const GfxProcessor&) = delete;

    GfxProcessor(std::unique_ptr<::mega::IGfxProvider> gfxProvider) :
        mGfxProvider{std::move(gfxProvider)}
    {
        assert(mGfxProvider);
    }

    GfxTaskResult process(const GfxTask& task);

    std::string supportedformats() const;

    std::string supportedvideoformats() const;

    virtual ~GfxProcessor() = default;

    static std::unique_ptr<GfxProcessor> create();
private:

    mega::FSACCESS_CLASS mFaccess;

    std::unique_ptr<::mega::IGfxProvider> mGfxProvider;
};

class RequestProcessor
{
public:
    RequestProcessor(std::unique_ptr<GfxProcessor> processor, size_t threadCount = 6, size_t maxQueueSize = 12);

    // process the request. return true if processsing should
    // be stopped such as received a shutdown request
    bool process(std::unique_ptr<IEndpoint> endpoint);

private:
    void processHello(IEndpoint* endpoint);

    void processShutDown(IEndpoint* endpoint);

    void processGfx(IEndpoint* endpoint, CommandNewGfx* request);

    void processSupportFormats(IEndpoint* endpoint);

    std::unique_ptr<GfxProcessor> mGfxProcessor;

    ThreadPool mThreadPool;

    const static TimeoutMs READ_TIMEOUT;

    const static TimeoutMs WRITE_TIMEOUT;
};

} //namespace gfx
} //namespace mega
