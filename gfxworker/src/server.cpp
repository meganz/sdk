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
#include "mega/gfx.h"

#include <iterator>
#include <numeric>
#include <algorithm>

using gfx::comms::CommandType;
using gfx::comms::ProtocolReader;
using gfx::comms::ProtocolWriter;
using gfx::comms::CommandNewGfx;
using gfx::comms::CommandNewGfxResponse;
using gfx::comms::CommandShutDown;
using gfx::comms::CommandShutDownResponse;
using gfx::ThreadPool;
using mega::LocalPath;
using Dimension = mega::IGfxProvider::Dimension;
namespace gfx {
namespace server {

GfxTaskResult GfxProcessor::process(const GfxTask& task)
{
    std::vector<std::string> outputImages(task.Sizes.size());

    if (task.Sizes.empty())
    {
        LOG_err << "Received empty sizes for " << task.Path;
        return GfxTaskResult(std::move(outputImages), GfxTaskProcessStatus::ERR);
    }

    // descending sort Sizes index for its width
    using SizeType = decltype(task.Sizes.size());
    auto& Sizes = task.Sizes;
    std::vector<SizeType> indices(task.Sizes.size());
    std::iota(std::begin(indices), std::end(indices), 0);
    std::sort(std::begin(indices),
              std::end(indices),
              [&Sizes](SizeType i1, SizeType i2)
              { 
                  return Sizes[i1].w() > Sizes[i2].w();
              });

    // new Dimensions based on sorted indices
    std::vector<Dimension> dimensions;
    std::transform(std::begin(indices),
                   std::end(indices),
                   std::back_insert_iterator<std::vector<Dimension>>(dimensions),
                   [&Sizes](SizeType i){ return Dimension{ Sizes[i].w(), Sizes[i].h()}; });

    // generate thumbnails
    auto images = mGfxProvider->generateImages(&mFaccess, 
                                               LocalPath::fromPlatformEncodedAbsolute(task.Path),
                                               dimensions);

    // assign back to original order
    for (int i = 0; i < images.size(); ++i)
    {
        outputImages[indices[i]] = std::move(images[i]);
    }

    return GfxTaskResult(std::move(outputImages), GfxTaskProcessStatus::SUCCESS);
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
