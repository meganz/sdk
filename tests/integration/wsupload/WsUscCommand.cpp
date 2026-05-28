#include "headers/WsUscCommand.h"

#include "headers/WsOneShotHelper.h"
#include "megaapi_impl.h"
#include "mega/commands_ws.h"
#include "mega/scoped_helpers.h"

#include <future>
#include <memory>
#include <string>
#include <utility>

namespace mega::test::wsupload
{

bool fetchUscSizeClasses(::mega::MegaApi& api,
                         std::vector<int64_t>& maxSizes,
                         int timeoutSeconds)
{
    using SizeClass = ::mega::CommandUSCForWsUpload::SizeClass;
    std::vector<SizeClass> classes;

    const bool dispatched = runOnClientThreadWithResult<std::vector<SizeClass>>(
        api, classes, timeoutSeconds,
        [](::mega::MegaClient* client,
           std::shared_ptr<std::promise<std::vector<SizeClass>>> promise)
        {
            if (!client)
            {
                promise->set_value({});
                return;
            }

            client->queueCommand(new ::mega::CommandUSCForWsUpload(
                *client,
                [promise](::mega::Error e, std::vector<SizeClass>&& result)
                {
                    if (e != ::mega::API_OK)
                    {
                        promise->set_value({});
                        return;
                    }
                    promise->set_value(std::move(result));
                }));
        });

    if (!dispatched || classes.empty())
    {
        return false;
    }

    maxSizes.clear();
    maxSizes.reserve(classes.size());
    for (const auto& entry : classes)
    {
        maxSizes.push_back(entry.second);
    }
    return true;
}

} // namespace mega::test::wsupload
