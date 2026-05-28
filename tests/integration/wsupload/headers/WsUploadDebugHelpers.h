/**
 * @file WsUploadDebugHelpers.h
 * @brief WS-upload DEBUG-only helpers: pool-state / stats fetchers, network-disconnect /
 *        engine-restart hooks.
 *
 * Extracted from SdkTest_test.cpp anonymous-namespace helpers gated under
 * `#ifdef MEGA_USE_WSUPLOAD && MEGASDK_DEBUG_TEST_HOOKS_ENABLED`. Header-only inline.
 *
 * Since the hook-ABI redesign (Option a — always-compile hook ABI), the file is
 * UNGATED at the file scope. The 5 inline free functions are odr-used
 * only from hooks-ON tests; the test fixtures runtime-gate their
 * invocation behind WSUPLOAD_REQUIRE_TEST_HOOKS() which expands to
 * GTEST_SKIP() in Release. Release builds carry ~150 LOC of dead inline-
 * helper bodies linked in but never reached — negligible.
 *
 * Default `timeoutSeconds` values match SdkTest_test.h's `defaultTimeout = 60` constant
 * (inlined as a literal).
 *
 * The promise/`ExecuteOnce`/future boilerplate is now factored into
 * `runOnClientThreadWithResult` from `WsOneShotHelper.h` (D1 refactor).
 */

#pragma once

#include "megaapi.h"
#include "megaapi_impl.h"
#include "mega/wsupload.h"
#include "wsupload/headers/WsOneShotHelper.h"

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <utility>

namespace mega::test::wsupload
{

inline bool fetchWsUploadPoolStateForTesting(::mega::MegaApi& api,
                                             const std::string& url,
                                             ::mega::ws::UploadEngine::PoolStateForTesting& out,
                                             int timeoutSeconds = 60)
{
    return runOnClientThreadWithResult<::mega::ws::UploadEngine::PoolStateForTesting>(
        api, out, timeoutSeconds,
        [url](::mega::MegaClient* client,
              std::shared_ptr<std::promise<::mega::ws::UploadEngine::PoolStateForTesting>>
                  promise)
        {
            ::mega::ws::UploadEngine::PoolStateForTesting state;
            if (client && client->wsEngine())
            {
                client->wsEngine()->getPoolStateForTesting(url, state);
            }
            promise->set_value(std::move(state));
        });
}

inline bool fetchWsUploadStatsForTesting(
    ::mega::MegaApi& api,
    ::mega::ws::UploadEngine::WsUploadStatsForTesting& out,
    int timeoutSeconds = 60)
{
    return runOnClientThreadWithResult<::mega::ws::UploadEngine::WsUploadStatsForTesting>(
        api, out, timeoutSeconds,
        [](::mega::MegaClient* client,
           std::shared_ptr<std::promise<::mega::ws::UploadEngine::WsUploadStatsForTesting>>
               promise)
        {
            ::mega::ws::UploadEngine::WsUploadStatsForTesting stats;
            if (client && client->wsEngine())
            {
                client->wsEngine()->getWsUploadStatsForTesting(stats);
            }
            promise->set_value(std::move(stats));
        });
}

inline bool waitForWsUploadPoolStateForTesting(
    ::mega::MegaApi& api,
    const std::string& url,
    ::mega::ws::UploadEngine::PoolStateForTesting& out,
    const std::function<bool(const ::mega::ws::UploadEngine::PoolStateForTesting&)>& predicate,
    int timeoutSeconds = 60,
    unsigned pollMillis = 200)
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(timeoutSeconds))
    {
        ::mega::ws::UploadEngine::PoolStateForTesting state{};
        if (fetchWsUploadPoolStateForTesting(api, url, state, 1) && predicate(state))
        {
            out = std::move(state);
            return true;
        }

        ::WaitMillisec(pollMillis);
    }

    return false;
}

inline bool notifyWsUploadNetworkDisconnectForTesting(::mega::MegaApi& api,
                                                      int timeoutSeconds = 60)
{
    bool notified = false;
    if (!runOnClientThreadWithResult<bool>(
            api, notified, timeoutSeconds,
            [](::mega::MegaClient* client, std::shared_ptr<std::promise<bool>> promise)
            {
                bool ok = false;
                if (client && client->wsEngine())
                {
                    client->wsEngine()->notifyNetworkDisconnect();
                    ok = true;
                }
                promise->set_value(ok);
            }))
    {
        return false;
    }
    return notified;
}

inline bool restartWsUploadEngineForTesting(::mega::MegaApi& api, int timeoutSeconds = 60)
{
    bool restarted = false;
    if (!runOnClientThreadWithResult<bool>(
            api, restarted, timeoutSeconds,
            [](::mega::MegaClient* client, std::shared_ptr<std::promise<bool>> promise)
            {
                bool ok = false;
                if (client && client->wsEngine())
                {
                    client->wsEngine()->stop();
                    client->wsEngine()->start();
                    client->wsEngine()->kick();
                    ok = true;
                }
                promise->set_value(ok);
            }))
    {
        return false;
    }
    return restarted;
}

// Release-safe — drains the per-iter throttle snapshot from
// `UploadEngine::getAndResetBenchThrottleStats()`. Unlike the sibling
// `fetchWsUploadStatsForTesting` (which is `MEGASDK_DEBUG_TEST_HOOKS_ENABLED`-
// gated via the DEBUG-only counters), this function returns a meaningful
// snapshot in any build that links the WS engine. Returns true iff the
// request completed within `timeoutSeconds`.
inline bool fetchAndResetWsUploadBenchThrottleStats(
    ::mega::MegaApi& api,
    ::mega::ws::UploadEngine::BenchThrottleSnapshot& out,
    int timeoutSeconds = 60)
{
    out = {};
    return runOnClientThreadWithResult<::mega::ws::UploadEngine::BenchThrottleSnapshot>(
        api, out, timeoutSeconds,
        [](::mega::MegaClient* client,
           std::shared_ptr<std::promise<::mega::ws::UploadEngine::BenchThrottleSnapshot>>
               promise)
        {
            ::mega::ws::UploadEngine::BenchThrottleSnapshot snap;
            if (client && client->wsEngine())
            {
                snap = client->wsEngine()->getAndResetBenchThrottleStats();
            }
            promise->set_value(snap);
        });
}

} // namespace mega::test::wsupload
