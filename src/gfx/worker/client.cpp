#include "mega/gfx/worker/client.h"
#include "mega/gfx/worker/command_serializer.h"
#include "mega/gfx/worker/commands.h"
#include "mega/logging.h"
#include "mega/filesystem.h"
#include "mega/types.h"
#include <memory>

#ifdef _WIN32
#include "mega/win32/gfx/worker/comms_client.h"
#endif

namespace mega {
namespace gfx {

bool GfxClient::runHello(const std::string& text)
{
    CommandHello command;
    command.Text = text;

    auto response = sendAndReceive<CommandHelloResponse>(command);
    if (response)
    {
        LOG_verbose << "GfxClient gets hello response: " << response->Text;
        return true;
    }
    else
    {
        LOG_err << "GfxClient couldn't get hello response";
        return false;
    }
}

bool GfxClient::runShutDown()
{
    if (sendAndReceive<CommandShutDownResponse>(CommandShutDown{}))
    {
        LOG_verbose << "GfxClient gets shutdown response";
        return true;
    }
    else
    {
        LOG_err << "GfxClient couldn't get shutdown response";
        return false;
    }
}

bool GfxClient::runGfxTask(const std::string& localpath, std::vector<GfxSize> sizes, std::vector<std::string>& images)
{
    // command
    CommandNewGfx command;
    command.Task.Path =  LocalPath::fromAbsolutePath(localpath).platformEncoded();
    command.Task.Sizes = std::move(sizes);

    auto addReponse = sendAndReceive<CommandNewGfxResponse>(command);
    if (!addReponse)
    {
        LOG_err << "GfxClient couldn't get gfxTask response";
        return false;
    }
    else if (addReponse->ErrorCode == static_cast<uint32_t>(GfxTaskProcessStatus::ERR))
    {
        LOG_info << "GfxClient gets gfxTask response with error: " << addReponse->ErrorText;
        return false;
    }
    else
    {
        LOG_verbose << "GfxClient gets gfxTask response successfully";
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

template<typename ResponseT, typename RequestT>
std::unique_ptr<ResponseT> GfxClient::sendAndReceive(RequestT command)
{
    // connect
    auto endpoint = mComms->connect();
    if (!endpoint)
    {
        LOG_err << "GfxClient couldn't connect";
        return nullptr;
    }

    // send a request
    ProtocolWriter writer(endpoint.get());
    writer.writeCommand(&command, TimeoutMs(5000));

    // get the response
    ProtocolReader reader(endpoint.get());
    auto response = reader.readCommand(TimeoutMs(5000));
    if (!dynamic_cast<ResponseT*>(response.get()))
    {
        LOG_err << "GfxClient couldn't get response";
        return nullptr;
    }
    else
    {
        LOG_verbose << "GfxClient gets response";
        return std::unique_ptr<ResponseT>(static_cast<ResponseT*>(response.release()));
    }
}

}
}
