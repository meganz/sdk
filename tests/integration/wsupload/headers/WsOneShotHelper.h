/**
 * @file WsOneShotHelper.h
 * @brief One-shot dispatcher to marshal a body onto the MegaClient thread and
 *        wait for its result via a `std::promise`.
 *
 * Consolidates the 9 `std::make_shared<::mega::ExecuteOnce>(...)` +
 * `std::promise<R> + future.wait_for(...)` boilerplate sites previously open-
 * coded across `WsUscCommand.cpp`, `WsUploadDebugHelpers.h` and
 * `WsUploadTestHelpers.h`.
 *
 * The helper preserves byte-identical capture types, one-shot semantics, and
 * the `if (!impl) return false;` early-return contract of every callsite. See
 * `Goal4_refactors/d1_design.md` for the migration table.
 *
 * Header-only / fully inline so consumers may include without ODR concerns.
 */

#pragma once

#include "megaapi.h"
#include "megaapi_impl.h"

#include <chrono>
#include <future>
#include <memory>
#include <utility>

namespace mega::test::wsupload
{

// Marshal `body` onto the MegaClient thread via `ExecuteOnce`, then wait up
// to `timeoutSeconds` for the body to set the promise.
//
// `body` shape: `void(::mega::MegaClient* client,
//                     std::shared_ptr<std::promise<R>> promise)`. The body
// MUST call `promise->set_value(...)` exactly once before the timeout, either
// synchronously (most callers) or asynchronously (e.g. WsUscCommand which
// queues an SDK command that calls `set_value` on completion).
//
// `client` may be null on test-fixture teardown races; the body owns the
// null-check + an appropriate default `promise->set_value(...)` in that case.
//
// Returns `true` iff the promise was set within the timeout window. On
// `true`, `out` holds the body-produced value. On `false`, `out` is left
// untouched.
template <typename R, typename Body>
inline bool runOnClientThreadWithResult(::mega::MegaApi& api,
                                        R& out,
                                        int timeoutSeconds,
                                        Body body)
{
    ::mega::MegaApiImpl* impl = ::mega::MegaApiImpl::ImplOf(&api);
    if (!impl)
    {
        return false;
    }

    auto promise = std::make_shared<std::promise<R>>();
    auto future = promise->get_future();

    auto exec = std::make_shared<::mega::ExecuteOnce>(
        [impl, promise, body = std::move(body)]() mutable
        {
            body(impl->getClientForTesting(), promise);
        });

    impl->executeOnThreadForTesting(exec);

    if (future.wait_for(std::chrono::seconds(timeoutSeconds)) !=
        std::future_status::ready)
    {
        return false;
    }

    out = future.get();
    return true;
}

} // namespace mega::test::wsupload
