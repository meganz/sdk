/**
 * @file WsUploadDebugHelpers.h
 * @brief WS-upload DEBUG-only helpers: pool-state / stats fetchers, network-disconnect /
 *        engine-restart hooks.
 *
 * Extracted from SdkTest_test.cpp anonymous-namespace helpers gated under
 * `#ifdef MEGA_USE_WSUPLOAD && MEGASDK_DEBUG_TEST_HOOKS_ENABLED`. Header-only inline.
 *
 * Since fu7-15 G2.a-2 (Option a — always-compile hook ABI), the file is
 * UNGATED at the file scope. The 5 inline free functions are odr-used
 * only from hooks-ON tests; the test fixtures runtime-gate their
 * invocation behind WSUPLOAD_REQUIRE_TEST_HOOKS() which expands to
 * GTEST_SKIP() in Release. Release builds carry ~150 LOC of dead inline-
 * helper bodies linked in but never reached — negligible.
 *
 * Default `timeoutSeconds` values match SdkTest_test.h's `defaultTimeout = 60` constant
 * (inlined as a literal).
 */

#pragma once

#include "megaapi.h"
#include "megaapi_impl.h"
#include "mega/wsupload.h"

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
    ::mega::MegaApiImpl* impl = ::mega::MegaApiImpl::ImplOf(&api);
    if (!impl)
    {
        return false;
    }

    auto promise =
        std::make_shared<std::promise<::mega::ws::UploadEngine::PoolStateForTesting>>();
    auto future = promise->get_future();

    auto exec = std::make_shared<::mega::ExecuteOnce>(
        [impl, promise, url]()
        {
            ::mega::ws::UploadEngine::PoolStateForTesting state;
            ::mega::MegaClient* client = impl->getClientForTesting();
            if (client && client->wsEngine())
            {
                client->wsEngine()->getPoolStateForTesting(url, state);
            }
            promise->set_value(std::move(state));
        });

    impl->executeOnThreadForTesting(exec);

    if (future.wait_for(std::chrono::seconds(timeoutSeconds)) != std::future_status::ready)
    {
        return false;
    }

    out = future.get();
    return true;
}

inline bool fetchWsUploadStatsForTesting(
    ::mega::MegaApi& api,
    ::mega::ws::UploadEngine::WsUploadStatsForTesting& out,
    int timeoutSeconds = 60)
{
    ::mega::MegaApiImpl* impl = ::mega::MegaApiImpl::ImplOf(&api);
    if (!impl)
    {
        return false;
    }

    auto promise =
        std::make_shared<std::promise<::mega::ws::UploadEngine::WsUploadStatsForTesting>>();
    auto future = promise->get_future();

    auto exec = std::make_shared<::mega::ExecuteOnce>(
        [impl, promise]()
        {
            ::mega::ws::UploadEngine::WsUploadStatsForTesting stats;
            ::mega::MegaClient* client = impl->getClientForTesting();
            if (client && client->wsEngine())
            {
                client->wsEngine()->getWsUploadStatsForTesting(stats);
            }
            promise->set_value(std::move(stats));
        });

    impl->executeOnThreadForTesting(exec);

    if (future.wait_for(std::chrono::seconds(timeoutSeconds)) != std::future_status::ready)
    {
        return false;
    }

    out = future.get();
    return true;
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
    ::mega::MegaApiImpl* impl = ::mega::MegaApiImpl::ImplOf(&api);
    if (!impl)
    {
        return false;
    }

    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();

    auto exec = std::make_shared<::mega::ExecuteOnce>(
        [impl, promise]()
        {
            bool notified = false;
            ::mega::MegaClient* client = impl->getClientForTesting();
            if (client && client->wsEngine())
            {
                client->wsEngine()->notifyNetworkDisconnect();
                notified = true;
            }
            promise->set_value(notified);
        });

    impl->executeOnThreadForTesting(exec);

    if (future.wait_for(std::chrono::seconds(timeoutSeconds)) != std::future_status::ready)
    {
        return false;
    }

    return future.get();
}

inline bool restartWsUploadEngineForTesting(::mega::MegaApi& api, int timeoutSeconds = 60)
{
    ::mega::MegaApiImpl* impl = ::mega::MegaApiImpl::ImplOf(&api);
    if (!impl)
    {
        return false;
    }

    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();

    auto exec = std::make_shared<::mega::ExecuteOnce>(
        [impl, promise]()
        {
            bool restarted = false;
            ::mega::MegaClient* client = impl->getClientForTesting();
            if (client && client->wsEngine())
            {
                client->wsEngine()->stop();
                client->wsEngine()->start();
                client->wsEngine()->kick();
                restarted = true;
            }
            promise->set_value(restarted);
        });

    impl->executeOnThreadForTesting(exec);

    if (future.wait_for(std::chrono::seconds(timeoutSeconds)) != std::future_status::ready)
    {
        return false;
    }

    return future.get();
}

// fu7-19 G7: Release-safe — drains the per-iter throttle snapshot from
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
    ::mega::MegaApiImpl* impl = ::mega::MegaApiImpl::ImplOf(&api);
    if (!impl)
    {
        return false;
    }

    auto promise =
        std::make_shared<std::promise<::mega::ws::UploadEngine::BenchThrottleSnapshot>>();
    auto future = promise->get_future();

    auto exec = std::make_shared<::mega::ExecuteOnce>(
        [impl, promise]()
        {
            ::mega::ws::UploadEngine::BenchThrottleSnapshot snap;
            ::mega::MegaClient* client = impl->getClientForTesting();
            if (client && client->wsEngine())
            {
                snap = client->wsEngine()->getAndResetBenchThrottleStats();
            }
            promise->set_value(snap);
        });

    impl->executeOnThreadForTesting(exec);

    if (future.wait_for(std::chrono::seconds(timeoutSeconds)) != std::future_status::ready)
    {
        return false;
    }

    out = future.get();
    return true;
}

} // namespace mega::test::wsupload
