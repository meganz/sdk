#include "mega/gfx/worker/client.h"
#include "mega/gfx/worker/command_serializer.h"
#include "mega/gfx/worker/commands.h"
#include "mega/logging.h"
#include "mega/filesystem.h"
#include <stdint.h>

#ifdef _WIN32
#include "mega/win32/gfx/worker/comms_client.h"
#endif

#include <thread>
#include <fstream>

namespace mega {
namespace gfx {

bool GfxClient::runShutDown()
{
    // connect
    auto endpoint = mComms->connect();
    if (!endpoint)
    {
        LOG_err << "GfxClient couldn't connect";
        return false;
    }

    // command
    CommandShutDown command;

    // send a request
    ProtocolWriter writer(endpoint.get());
    writer.writeCommand(&command, TimeoutMs(5000));

    // get the response
    ProtocolReader reader(endpoint.get());
    auto response = reader.readCommand(TimeoutMs(5000));
    if (!dynamic_cast<CommandShutDownResponse*>(response.get()))
    {
        LOG_err << "GfxClient couldn't get response";
        return false;
    }
    else
    {
        LOG_info << "GfxClient gets shutdown response";
        return true;
    }
}

bool GfxClient::runGfxTask(const std::string& localpath, std::vector<GfxSize> sizes, std::vector<std::string>& images)
{
    // connect
    auto endpoint = mComms->connect();
    if (!endpoint)
    {
        LOG_err << "GfxClient couldn't connect";
        return false;
    }

    // command
    CommandNewGfx command;
    command.Task.Path =  LocalPath::fromAbsolutePath(localpath).platformEncoded();
    command.Task.Sizes = std::move(sizes);

    // send a request
    ProtocolWriter writer(endpoint.get());
    writer.writeCommand(&command, TimeoutMs(5000));

    // get the response
    ProtocolReader reader(endpoint.get());
    auto response = reader.readCommand(TimeoutMs(5000));
    CommandNewGfxResponse* addReponse = dynamic_cast<CommandNewGfxResponse*>(response.get());
    if (!addReponse)
    {
        LOG_err << "GfxClient couldn't get response";
        return false;
    }
    else if (addReponse->ErrorCode == static_cast<uint32_t>(GfxTaskProcessStatus::ERR))
    {
        LOG_info << "GfxClient gets error response: " << addReponse->ErrorText;
        return false;
    }
    else
    {
        images = std::move(addReponse->Images);
        return true;
    }
}

GfxClient GfxClient::create()
{
#ifdef _WIN32
    return GfxClient(mega::make_unique<WinGfxCommunicationsClient>([](std::unique_ptr<IEndpoint> _) {}));
#else
    return GfxClient(nullptr);
#endif
}

}
}
