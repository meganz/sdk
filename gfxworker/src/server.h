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

struct IGfxProcessor
{
    virtual ~IGfxProcessor() = default;

    virtual GfxTaskResult process(const GfxTask& task) = 0;

    virtual std::string supportedformats() const = 0;

    virtual std::string supportedvideoformats() const = 0;
};

class GfxProcessor : public IGfxProcessor
{
public:
    GfxProcessor() = delete;

    GfxProcessor(const GfxProcessor&) = delete;

    GfxProcessor(std::unique_ptr<::mega::IGfxProvider> gfxProvider) :
        mGfxProvider{std::move(gfxProvider)}
    {
        assert(mGfxProvider);
    }

    GfxTaskResult process(const GfxTask& task) override;

    std::string supportedformats() const override;

    std::string supportedvideoformats() const override;

    virtual ~GfxProcessor() = default;

    static std::unique_ptr<GfxProcessor> create();
private:

    mega::FSACCESS_CLASS mFaccess;

    std::unique_ptr<::mega::IGfxProvider> mGfxProvider;
};

class IRequestProcessor
{
public:
    virtual ~IRequestProcessor() = default;

    // process the request. return true if processsing should
    // be stopped such as received a shutdown request
    virtual bool process(std::unique_ptr<IEndpoint> endpoint) = 0;
};

class RequestProcessor : public IRequestProcessor
{
public:
    RequestProcessor(std::unique_ptr<IGfxProcessor> processor, size_t threadCount = 6, size_t maxQueueSize = 12);

    bool process(std::unique_ptr<IEndpoint> endpoint);

private:
    void processHello(IEndpoint* endpoint);

    void processShutDown(IEndpoint* endpoint);

    void processGfx(IEndpoint* endpoint, CommandNewGfx* request);

    void processSupportFormats(IEndpoint* endpoint);

    ThreadPool mThreadPool;

    std::unique_ptr<IGfxProcessor> mGfxProcessor;

    const static TimeoutMs READ_TIMEOUT;

    const static TimeoutMs WRITE_TIMEOUT;
};

} //namespace gfx
} //namespace mega
