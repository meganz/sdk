/**
 * @file WsQuotaClientThread.h
 * @brief Client-thread trigger for the SDK-6298 usl-change (M1) entry point.
 *
 * Tests cannot write the server-managed `^!usl` attribute, so they model a usl
 * change by invoking `MegaClient::wsQuotaInvalidateAndMarkDirty()` (M1) directly
 * on the client thread. This bumps the quota generation and marks the ledger
 * dirty, so the next exec cycle issues a fresh `tfs` and re-evaluates holds —
 * exactly what a real usl transition does via setstoragestatus (see
 * analysis/DESIGN_IMPL_SDK6298.md Q6/Q9-M1).
 *
 * Uses the WsUscCommand client-thread execution pattern
 * (runOnClientThreadWithResult from WsOneShotHelper.h). Requires the full
 * MegaClient definition (mega.h), mirroring WsUploadTestHelpers.h.
 *
 * Pre-implementation NOTE: `wsQuotaInvalidateAndMarkDirty()` is a null-safe
 * no-op until P3 constructs mWsQuota, so this call dispatches cleanly but has no
 * observable effect yet. Header-only / fully inline; `::mega::` prefixes (C++20).
 */

#pragma once

#include "mega.h" // MegaClient full type (wsQuotaInvalidateAndMarkDirty)
#include "megaapi.h"
#include "wsupload/headers/WsOneShotHelper.h"

#include <future>
#include <memory>

namespace mega::test::wsupload
{

// Dispatches MegaClient::wsQuotaInvalidateAndMarkDirty() onto the client thread
// and waits for it to run. Returns true iff the client thread executed the call
// within the timeout (false on teardown races / timeout).
inline bool invokeWsQuotaInvalidateOnClientThread(::mega::MegaApi& api, int timeoutSeconds = 60)
{
    bool ran = false;
    const bool dispatched = runOnClientThreadWithResult<bool>(
        api,
        ran,
        timeoutSeconds,
        [](::mega::MegaClient* client, std::shared_ptr<std::promise<bool>> promise)
        {
            if (client)
            {
                client->wsQuotaInvalidateAndMarkDirty();
                promise->set_value(true);
            }
            else
            {
                promise->set_value(false);
            }
        });
    return dispatched && ran;
}

// Dispatches MegaClient::setstoragestatus(status) onto the client thread and waits
// for it to run. Models a real server-driven usl storage-status transition END TO
// END (tests cannot write `^!usl`): a transition OUT of overquota (e.g. RED ->
// GREEN) makes the SDK's setstoragestatus fire abortbackoff(true) — re-arming the
// NEVER backoff that activateoverquota imposes on every PUT — AND
// wsQuotaInvalidateAndMarkDirty() (Q6: a fresh tfs re-evaluates holds). Use it to
// recover the account after an (injected) account-RED overquota so previously
// blocked/held uploads can resume. Returns true iff the client thread executed the
// call within the timeout (false on teardown races / timeout).
inline bool setStorageStatusOnClientThread(::mega::MegaApi& api,
                                           ::mega::storagestatus_t status,
                                           int timeoutSeconds = 60)
{
    bool ran = false;
    const bool dispatched = runOnClientThreadWithResult<bool>(
        api,
        ran,
        timeoutSeconds,
        [status](::mega::MegaClient* client, std::shared_ptr<std::promise<bool>> promise)
        {
            if (client)
            {
                client->setstoragestatus(status);
                promise->set_value(true);
            }
            else
            {
                promise->set_value(false);
            }
        });
    return dispatched && ran;
}

} // namespace mega::test::wsupload
