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
    CommandShutDown command;
    if (sendAndReceive<CommandShutDownResponse>(command))
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

bool GfxClient::runGfxTask(const std::string& localpath,
                           const std::vector<GfxDimension>& dimensions,
                           std::vector<std::string>& images)
{
    CommandNewGfx command;
    command.Task.Path =  LocalPath::fromAbsolutePath(localpath).platformEncoded();
    command.Task.Dimensions = dimensions;

    auto addReponse = sendAndReceive<CommandNewGfxResponse>(command);
    if (!addReponse)
    {
        LOG_err << "GfxClient couldn't get gfxTask response, " << command.Task.Path;
        return false;
    }
    else if (addReponse->ErrorCode == static_cast<uint32_t>(GfxTaskProcessStatus::ERR))
    {
        LOG_info << "GfxClient gets gfxTask response with error: "
                 << addReponse->ErrorText
                 << ", "
                 <<  command.Task.Path;
        return false;
    }
    else
    {
        LOG_verbose << "GfxClient gets gfxTask response successfully, " << command.Task.Path;
        images = std::move(addReponse->Images);
        return true;
    }
}

bool GfxClient::runSupportFormats(std::string& formats, std::string& videoformats)
{
    CommandSupportFormats command;

    auto reponse = sendAndReceive<CommandSupportFormatsResponse>(command);
    if (!reponse)
    {
        LOG_err << "GfxClient couldn't get supportformats response";
        return false;
    }
    else
    {
        formats = std::move(reponse->formats);
        videoformats = std::move(reponse->videoformats);
        return true;
    }
}

GfxClient GfxClient::create(const std::string& pipename)
{
#ifdef _WIN32
    return GfxClient(mega::make_unique<WinGfxCommunicationsClient>(pipename));
#else
    // To implement
    return GfxClient(pipename, nullptr);
#endif
}

template<typename ResponseT, typename RequestT>
std::unique_ptr<ResponseT> GfxClient::sendAndReceive(RequestT command, TimeoutMs sendTimeout, TimeoutMs receiveTimeout)
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
    writer.writeCommand(&command, sendTimeout);

    // get the response
    ProtocolReader reader(endpoint.get());
    auto response = reader.readCommand(receiveTimeout);
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
