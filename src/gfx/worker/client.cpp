#include "mega/gfx/worker/client.h"
#include "mega/gfx/worker/command_serializer.h"
#include "mega/gfx/worker/commands.h"
#include "mega/gfx/worker/comms.h"
#include "mega/logging.h"
#include "mega/filesystem.h"
#include "mega/types.h"
#include <chrono>
#include <memory>
#include <thread>
#include <tuple>
#include <unordered_set>


using std::chrono::milliseconds;
namespace mega {
namespace gfx {

GfxClient::GfxClient(std::unique_ptr<IGfxCommunicationsClient> comms) : mComms{std::move(comms)}
{
        assert(mComms);
}

bool GfxClient::runHello(const std::string& text)
{
    auto endpoint = connect();
    if (!endpoint)
    {
        LOG_err << "runHello Couldn't connect";
        return false;
    }

    CommandHello command;
    command.Text = text;

    auto response = sendAndReceive<CommandHelloResponse>(endpoint.get(), command);
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
    auto endpoint = connect();
    if (!endpoint)
    {
        LOG_err << "runShutDown Couldn't connect";
        return false;
    }

    CommandShutDown command;
    if (sendAndReceive<CommandShutDownResponse>(endpoint.get(), command))
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
     // 3 seconds at most
    auto endpoint = connectWithRetry(milliseconds(100), 30);
    if (!endpoint)
    {
        LOG_err << "runGfxTask Couldn't connect";
        return false;
    }

    CommandNewGfx command;
    command.Task.Path =  LocalPath::fromAbsolutePath(localpath).platformEncoded();
    command.Task.Dimensions = dimensions;

    auto addReponse = sendAndReceive<CommandNewGfxResponse>(endpoint.get(), command);
    if (!addReponse)
    {
        LOG_err << "GfxClient couldn't get gfxTask response, " << localpath;
        return false;
    }
    else if (addReponse->ErrorCode == static_cast<uint32_t>(GfxTaskProcessStatus::ERR))
    {
        LOG_info << "GfxClient gets gfxTask response with error: "
                 << addReponse->ErrorText
                 << ", "
                 <<  localpath;
        return false;
    }
    else
    {
        LOG_verbose << "GfxClient gets gfxTask response successfully, " << localpath;
        images = std::move(addReponse->Images);
        return true;
    }
}

bool GfxClient::runSupportFormats(std::string& formats, std::string& videoformats)
{
    auto endpoint = connectWithRetry(milliseconds(100), 30); // 3 seconds at most
    if (!endpoint)
    {
        LOG_err << "runSupportFormats Couldn't connect";
        return false;
    }

    CommandSupportFormats command;

    auto reponse = sendAndReceive<CommandSupportFormatsResponse>(endpoint.get(), command);
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

GfxClient GfxClient::create(const std::string& endpointName)
{
    return GfxClient(std::make_unique<GfxCommunicationsClient>(endpointName));
}

//
// CommError::NOT_EXIST is returned when server is not running, this could due to server is restarted
//
bool GfxClient::isRetryError(CommError error) const
{
    static const std::unordered_set<CommError> retryErrors = {
        CommError::NOT_EXIST
    };

    return retryErrors.find(error) != retryErrors.end();
}

std::unique_ptr<IEndpoint> GfxClient::connectWithRetry(milliseconds backoff, unsigned int maxRetries)
{
    unsigned int loop = 0;
    do {
        auto [error, endpoint] = mComms->connect();

        // connected
        if (endpoint)
        {
            return std::move(endpoint); // endpoint is reference
        }

        if (++loop > maxRetries)
        {
            return nullptr;
        }

        if (isRetryError(error))
        {
            std::this_thread::sleep_for(backoff);
            continue;
        }
        else
        {
            return nullptr;
        }
    }while (true);
}

std::unique_ptr<IEndpoint> GfxClient::connect()
{
    return connectWithRetry(milliseconds(0), 0);
}

template<typename ResponseT, typename RequestT>
std::unique_ptr<ResponseT> GfxClient::sendAndReceive(IEndpoint* endpoint, RequestT command, TimeoutMs sendTimeout, TimeoutMs receiveTimeout)
{
    // send a request
    ProtocolWriter writer(endpoint);
    writer.writeCommand(&command, sendTimeout);

    // get the response
    ProtocolReader reader(endpoint);
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
