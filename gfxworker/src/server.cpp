#include "server.h"
#include "threadpool.h"
#include "logger.h"
#include "mega/filesystem.h"
#include "mega/gfx.h"
#include "mega/gfx/worker/commands.h"
#include "mega/gfx/worker/comms.h"
#include "mega/gfx/worker/command_serializer.h"
#include "mega/logging.h"

#include <iterator>
#include <numeric>
#include <algorithm>

namespace mega {
namespace gfx {

const TimeoutMs RequestProcessor::READ_TIMEOUT(5000);

const TimeoutMs RequestProcessor::WRITE_TIMEOUT(5000);

std::unique_ptr<GfxProcessor> GfxProcessor::create()
{
    return ::mega::make_unique<GfxProcessor>(
        ::mega::make_unique<GfxProviderFreeImage>()
    );
}

GfxTaskResult GfxProcessor::process(const GfxTask& task)
{
    std::vector<std::string> outputImages(task.Dimensions.size());
    auto path = LocalPath::fromPlatformEncodedAbsolute(task.Path);
    if (task.Dimensions.empty())
    {
        LOG_err << "Received empty dimensions for " << path;
        return GfxTaskResult(std::move(outputImages), GfxTaskProcessStatus::ERR);
    }

    // descending sort Dimensions index for its width
    using SizeType = decltype(task.Dimensions.size());
    auto& dimensions = task.Dimensions;
    std::vector<SizeType> indices(task.Dimensions.size());
    std::iota(std::begin(indices), std::end(indices), 0);
    std::sort(std::begin(indices),
              std::end(indices),
              [&dimensions](SizeType i1, SizeType i2)
              {
                  return dimensions[i1].w() > dimensions[i2].w();
              });

    // new sorted dimensions based on sorted indices
    std::vector<GfxDimension> sortedDimensions;
    std::transform(std::begin(indices),
                   std::end(indices),
                   std::back_insert_iterator<std::vector<GfxDimension>>(sortedDimensions),
                   [&dimensions](SizeType i){ return GfxDimension{ dimensions[i].w(), dimensions[i].h()}; });

    // generate thumbnails
    LOG_info << "generate for, " << path;
    auto images = mGfxProvider->generateImages(&mFaccess,
                                               path,
                                               sortedDimensions);

    // assign back to original order
    for (int i = 0; i < images.size(); ++i)
    {
        outputImages[indices[i]] = std::move(images[i]);
    }

    return GfxTaskResult(std::move(outputImages), GfxTaskProcessStatus::SUCCESS);
}

//
// Put more probmatic format (likely crash) by freeimage here in extraFormatsByWorker
// note order by length of ext. If we has this order: .tiff.tif, the match with .tif fails
// see how GfxProc::isgfx is implemented.
//
std::string GfxProcessor::supportedformats() const
{
    std::string extraFormatsByWorker = ".tif.exr.pic.pct.tiff.pict";
    auto formats = mGfxProvider->supportedformats();
    return formats ? std::string(formats) + extraFormatsByWorker : "";
}

std::string GfxProcessor::supportedvideoformats() const
{
    auto videoformats = mGfxProvider->supportedvideoformats();
    return videoformats ? std::string(videoformats) : "";
}

RequestProcessor::RequestProcessor(std::unique_ptr<IGfxProcessor> processor,
                                   size_t threadCount,
                                   size_t maxQueueSize)
                                   : mGfxProcessor(std::move(processor))
                                   , mThreadPool(threadCount, maxQueueSize)
{
}

bool RequestProcessor::process(std::unique_ptr<IEndpoint> endpoint)
{
    bool stopRunning = false;

    // read command
    ProtocolReader reader{ endpoint.get() };
    std::shared_ptr<ICommand> command = reader.readCommand(READ_TIMEOUT);
    if (!command)
    {
        LOG_err << "command couldn't be unserialized";
        return stopRunning;
    }
    stopRunning = command->type() == CommandType::SHUTDOWN;

    // execute command
    LOG_info << "execute the command in the thread pool: "
             << static_cast<int>(command->type())
             << "/"
             << command->typeStr();

    // gfx processing might crash as processing bad images
    // we flush log for every request to avoid a large portion log lost
    MegaFileLogger::get().flush();

    std::shared_ptr<IEndpoint> sharedEndpoint = std::move(endpoint);

    mThreadPool.push(
        [sharedEndpoint, command, this]() {
            switch (command->type())
            {
            case CommandType::HELLO:
            {
                processHello(sharedEndpoint.get());
                break;
            }
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
            case CommandType::SUPPORT_FORMATS:
            {
                processSupportFormats(sharedEndpoint.get());
                break;
            }
            default:
                break;
            }
        });

    return stopRunning;
}

void RequestProcessor::processHello(IEndpoint* endpoint)
{
    CommandHelloResponse response;
    ProtocolWriter writer{ endpoint };
    writer.writeCommand(&response, WRITE_TIMEOUT);
}

void RequestProcessor::processShutDown(IEndpoint* endpoint)
{
    CommandShutDownResponse response;
    ProtocolWriter writer{ endpoint };
    writer.writeCommand(&response, WRITE_TIMEOUT);
}

void RequestProcessor::processGfx(IEndpoint* endpoint, CommandNewGfx* request)
{
    assert(endpoint);
    assert(request);

    LOG_info << "gfx processing";

    auto result = mGfxProcessor->process(request->Task);

    CommandNewGfxResponse response;
    response.ErrorCode = static_cast<uint32_t>(result.ProcessStatus);
    response.ErrorText = result.ProcessStatus == GfxTaskProcessStatus::SUCCESS ? "OK" : "ERROR";
    response.Images = std::move(result.OutputImages);

    LOG_info << "gfx result, " << response.ErrorText;

    ProtocolWriter writer{ endpoint };
    writer.writeCommand(&response, WRITE_TIMEOUT);
}

void RequestProcessor::processSupportFormats(IEndpoint* endpoint)
{
    assert(endpoint);

    CommandSupportFormatsResponse response;
    response.formats = mGfxProcessor->supportedformats();
    response.videoformats = mGfxProcessor->supportedvideoformats();

    ProtocolWriter writer{ endpoint };
    writer.writeCommand(&response, WRITE_TIMEOUT);
}

} //namespace server
} //namespace gfx
