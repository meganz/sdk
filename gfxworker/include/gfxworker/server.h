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

#include "gfxworker/comms.h"
#include "gfxworker/threadpool.h"
#include "gfxworker/tasks.h"
#include "gfxworker/commands.h"

#include "mega/gfx/freeimage.h"
#include "megafs.h"

namespace mega {
namespace gfx {

struct IGfxProcessor
{
    virtual ~IGfxProcessor() = default;
    virtual GfxTaskResult process(const GfxTask& task) = 0;
};

struct IGfxProcessorFactory
{
    virtual ~IGfxProcessorFactory() = default;
    virtual std::unique_ptr<IGfxProcessor> processor() = 0;
};

class GfxProcessor : public IGfxProcessor
{
    mega::FSACCESS_CLASS mFaccess;
    std::unique_ptr<::mega::IGfxProvider> mGfxProvider;
public:
    GfxProcessor() = delete;
    GfxProcessor(const GfxProcessor&) = delete;
    GfxProcessor(std::unique_ptr<::mega::IGfxProvider> &&gfxProvider) :
        mGfxProvider{std::move(gfxProvider)}
    {
        assert(mGfxProvider);
    }
    GfxTaskResult process(const GfxTask& task) override;
    virtual ~GfxProcessor() = default;
};

struct GfxProcessorFactory : public IGfxProcessorFactory
{
    std::unique_ptr<IGfxProcessor> processor() override
    {
        return std::unique_ptr<IGfxProcessor>(new GfxProcessor(
            std::unique_ptr<::mega::IGfxProvider>(new mega::GfxProviderFreeImage)
        ));
    }
};

using TaskIndex = long long int;

class IRequestProcessor
{
public:
    virtual ~IRequestProcessor() = default;
    virtual bool process(std::unique_ptr<mega::gfx::IEndpoint> endpoint) = 0;
};

class RequestProcessor : public IRequestProcessor
{
public:
    RequestProcessor(std::unique_ptr<IGfxProcessor> processor);

    bool process(std::unique_ptr<mega::gfx::IEndpoint> endpoint);

private:
    void processShutDown(mega::gfx::IEndpoint* endpoint);

    void processGfx(mega::gfx::IEndpoint* endpoint, mega::gfx::CommandNewGfx* request);

    ThreadPool mThreadPool;
    std::unique_ptr<IGfxProcessor> mGfxProcessor;
};

} //namespace gfx
} //namespace mega
