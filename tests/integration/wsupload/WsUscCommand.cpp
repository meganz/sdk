#include "headers/WsUscCommand.h"

#include "megaapi_impl.h"
#include "mega/commands_ws.h"
#include "mega/scoped_helpers.h"

#include <chrono>
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
    ::mega::MegaApiImpl* impl = ::mega::MegaApiImpl::ImplOf(&api);
    if (!impl)
    {
        return false;
    }

    using SizeClass = ::mega::CommandUSCForWsUpload::SizeClass;
    auto promise = std::make_shared<std::promise<std::vector<SizeClass>>>();
    auto future = promise->get_future();

    auto exec = std::make_shared<::mega::ExecuteOnce>(
        [impl, promise]()
        {
            ::mega::MegaClient* client = impl->getClientForTesting();
            if (!client)
            {
                promise->set_value({});
                return;
            }

            client->queueCommand(new ::mega::CommandUSCForWsUpload(
                *client,
                [promise](::mega::Error e, std::vector<SizeClass>&& classes)
                {
                    if (e != ::mega::API_OK)
                    {
                        promise->set_value({});
                        return;
                    }
                    promise->set_value(std::move(classes));
                }));
        });

    impl->executeOnThreadForTesting(exec);

    if (future.wait_for(std::chrono::seconds(timeoutSeconds)) != std::future_status::ready)
    {
        return false;
    }

    auto classes = future.get();
    if (classes.empty())
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
