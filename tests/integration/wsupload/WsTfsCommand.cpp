#include "headers/WsTfsCommand.h"

#include "headers/WsOneShotHelper.h"
#include "mega/commands_ws.h"
#include "megaapi_impl.h"

#include <future>
#include <memory>
#include <utility>
#include <vector>

namespace mega::test::wsupload
{

bool fetchTfsGroups(::mega::MegaApi& api,
                    const std::vector<::mega::NodeHandle>& folders,
                    ::mega::WsTfsGroupBalances& out,
                    ::mega::Error& err,
                    std::chrono::seconds timeout)
{
    // The `tfs` reply carries an error alongside the groups, so the promise value
    // must ferry both back to the caller (WsUscCommand only needed the groups).
    struct TfsResult
    {
        ::mega::Error err{::mega::API_EINTERNAL};
        ::mega::WsTfsGroupBalances groups;
    };

    TfsResult result;

    const bool dispatched = runOnClientThreadWithResult<TfsResult>(
        api,
        result,
        static_cast<int>(timeout.count()),
        // Capture `folders` BY VALUE: runOnClientThreadWithResult returns to the
        // caller on timeout, so a by-reference capture would dangle if the client
        // thread ran the body afterwards. The copy lives inside the queued
        // ExecuteOnce closure until it executes.
        [folders](::mega::MegaClient* client, std::shared_ptr<std::promise<TfsResult>> promise)
        {
            if (!client)
            {
                promise->set_value(TfsResult{::mega::Error(::mega::API_EINTERNAL), {}});
                return;
            }

            client->queueCommand(new ::mega::CommandTfsForWsUpload(
                *client,
                folders,
                [promise](::mega::Error e, ::mega::WsTfsGroupBalances&& groups)
                {
                    promise->set_value(TfsResult{e, std::move(groups)});
                }));
        });

    if (!dispatched)
    {
        err = ::mega::Error(::mega::API_EINTERNAL);
        return false;
    }

    err = result.err;
    out = std::move(result.groups);
    return true;
}

} // namespace mega::test::wsupload
