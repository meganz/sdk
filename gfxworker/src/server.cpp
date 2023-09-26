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

#include "gfxworker/server.h"
#include "mega/filesystem.h"
#include "gfxworker/comms.h"
#include "gfxworker/command_serializer.h"
#include "gfxworker/commands.h"
#include "gfxworker/utils.h"
#include "gfxworker/threadpool.h"

using gfx::comms::CommandType;
using gfx::comms::ProtocolReader;
using gfx::comms::ProtocolWriter;
using gfx::comms::CommandNewGfx;
using gfx::comms::CommandNewGfxResponse;
using gfx::comms::CommandShutDown;
using gfx::comms::CommandShutDownResponse;
using gfx::ThreadPool;
using mega::LocalPath;

namespace gfx {
namespace server {

GfxTaskResult GfxProcessor::process(const GfxTask& task)
{
    struct GfxTaskDataRef
    {
        GfxTaskDataRef(std::string& imageRef, const GfxSize& sizeRef)
            : mImgRef(&imageRef), mSizeRef(&sizeRef) {}
        std::string* mImgRef;
        const GfxSize* mSizeRef;
    };

    GfxTaskProcessStatus status = GfxTaskProcessStatus::SUCCESS;

    const std::vector<GfxSize>& sizes = task.Sizes;
    using sizes_t = decltype(sizes.size());
    const sizes_t numSizes = sizes.size();
    std::vector<std::string> outputImages;
    outputImages.resize(numSizes);
    if (numSizes == 0)
    {
        LOG_err << "Received empty sizes for " << task.Path;
        return GfxTaskResult(std::move(outputImages), GfxTaskProcessStatus::ERR);
    }

    // order from largest to smallest width
    std::vector<GfxTaskDataRef> orderedTaskData;
    orderedTaskData.reserve(numSizes);
    for (sizes_t i = 0; i < numSizes; i++)
    {
        const GfxSize& size = sizes[i];
        // note height can be zero
        if (size.w() <= 0 || size.h() < 0)
        {
            LOG_err << "Received zero or negative sizes for " << task.Path;
            return GfxTaskResult(std::move(outputImages), GfxTaskProcessStatus::ERR);
        }
        orderedTaskData.emplace_back(outputImages[i], size);
    }

    std::sort(orderedTaskData.begin(), orderedTaskData.end(),
        [](const GfxTaskDataRef& td1, const GfxTaskDataRef& td2)
        {
            return td1.mSizeRef->w() > td2.mSizeRef->w();
        });

    LocalPath localPath = LocalPath::fromPlatformEncodedAbsolute(task.Path);

    if (mGfxProvider->readbitmap(&mFaccess, localPath, orderedTaskData[0].mSizeRef->w()))
    {
        for (sizes_t i = 0; i < numSizes; i++)
        {
            int w = orderedTaskData[i].mSizeRef->w();
            int h = orderedTaskData[i].mSizeRef->h();
            if (mGfxProvider->width() < w && mGfxProvider->height() < h)
            {
                LOG_debug << "Skipping upsizing of preview or thumbnail for " << localPath;
                w = mGfxProvider->width();
                h = mGfxProvider->height();
            }
            if (!mGfxProvider->resizebitmap(w, h, orderedTaskData[i].mImgRef))
            {
                LOG_err << "Error resizing bitmap for " << localPath;
                status = GfxTaskProcessStatus::ERR;
            }
        }
        mGfxProvider->freebitmap();
    }
    else
    {
        LOG_err << "Error reading bitmap for " << localPath;
        status = GfxTaskProcessStatus::ERR;
    }
    return GfxTaskResult(std::move(outputImages), status);
}

RequestProcessor::RequestProcessor(std::unique_ptr<IGfxProcessor> processor) : mGfxProcessor(std::move(processor))
{
    mThreadPool.initialize(5, 10);
}

bool RequestProcessor::process(std::unique_ptr<gfx::comms::IEndpoint> endpoint)
{
    bool keepRunning = true;

    // read command
    ProtocolReader reader{ endpoint.get() };
    std::shared_ptr<gfx::comms::ICommand> command = reader.readCommand(5000);
    if (!command)
    {
        LOG_err << "command couldn't be unserialized";
        return keepRunning;
    }
    keepRunning = command->type() != CommandType::SHUTDOWN;

    // execute command
    LOG_info << "execute the command: " << static_cast<int>(command->type());

    std::shared_ptr<gfx::comms::IEndpoint> sharedEndpoint = std::move(endpoint);

    mThreadPool.push(
        [sharedEndpoint, command, this]() {
            switch (command->type())
            {
            case CommandType::SHUTDOWN:
            {
                processShutDown(sharedEndpoint.get());
                break;
            }
            case CommandType::NEW_GFX:
            {
                processGfx(sharedEndpoint.get(), dynamic_cast<CommandNewGfx*>(command.get()));
                break;
            }
            default:
                break;
            }
        });

    return keepRunning;
}

void RequestProcessor::processShutDown(gfx::comms::IEndpoint* endpoint)
{
    CommandShutDownResponse response;
    ProtocolWriter writer{ endpoint };
    writer.writeCommand(&response, 5000);
}

void RequestProcessor::processGfx(gfx::comms::IEndpoint* endpoint, CommandNewGfx* request)
{
    assert(endpoint);
    assert(request);

    auto result = mGfxProcessor->process(request->Task);

    CommandNewGfxResponse response;
    response.ErrorCode = static_cast<uint32_t>(result.ProcessStatus);
    response.ErrorText = result.ProcessStatus == GfxTaskProcessStatus::SUCCESS ? "OK" : "ERROR";
    
    response.Images = std::move(result.OutputImages);

    ProtocolWriter writer{ endpoint };
    writer.writeCommand(&response, 5000);
}
} //namespace server
} //namespace gfx
