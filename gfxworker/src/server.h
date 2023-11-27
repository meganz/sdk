/**
 * (c) 2013 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#pragma once

#include <memory>
#include <unordered_map>
#include <mutex>

#include "threadpool.h"

#include "mega/gfx/worker/comms.h"
#include "mega/gfx/worker/commands.h"
#include "mega/gfx/freeimage.h"
#include "mega/gfx/worker/tasks.h"
#include "megafs.h"

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
