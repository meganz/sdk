/**
 * @file src/transfer/ws/ws_pool_mgr.cpp
 * @brief WsPoolMgr lifecycle bodies — split out of wsupload.cpp.
 *
 *        Bodies hosted here:
 *
 *          - ctor / dtor
 *          - bumpLastNetRead / bumpAllPools
 *          - markPoolRetiring / poolHasNoWork / cleanupRetiringPools
 *          - applyRefreshBackoff
 *          - pinnedPoolHasReference / retireUnusedPinnedPools
 *          - checkPools
 *          - refreshPools
 *          - applyRefreshedUrls
 *          - pinnedPoolConnectionLimit
 *
 *        WsPoolMgr::curlIO and WsPoolMgr::ensurePinnedPool live in the sibling
 *        TU src/transfer/ws/ws_curl.cpp.
 *
 *        Includes:
 *          - mega/transfer/ws/ws_pool_mgr.h for the WsPoolMgr declaration.
 *          - mega/transfer/ws/wsupload_internal.h for the WsPool / SteadyTime
 *            / WSUPLOAD_TRACE cluster.
 *          - mega/transfer/ws/wsupload_engine.h for the full
 *            UploadEngine::Impl definition (needed by methods that dereference
 *            Impl members).
 *          - mega/transfer/ws/ws_upload_file.h for the WsUploadFile complete
 *            type (used by pinnedPoolHasReference, retireUnusedPinnedPools,
 *            refreshPools' wsPostToClientThread captures and applyRefreshedUrls
 *            via poolHasNoWork).
 *          - mega/commands_ws.h for CommandUSCForWsUpload used by refreshPools.
 *
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
 *
 * @copyright Simplified (2-clause) BSD License.
 */

#ifdef MEGA_USE_WSUPLOAD

#include "mega/transfer/ws/ws_pool_mgr.h"

// File-internal types shared with wsupload.cpp (WsPool, SteadyTime,
// WSUPLOAD_TRACE, etc.). SDK-internal architecture header.
#include "mega/transfer/ws/wsupload_internal.h"

// Full WsUploadFile definition. Used by the bodies below
// (pinnedPoolHasReference dereferences uf->mPool / uf->sessionUrlHint(),
// retireUnusedPinnedPools through pinnedPoolHasReference, applyRefreshedUrls
// indirectly via poolHasNoWork → pool.mUploadingFile).
#include "mega/transfer/ws/ws_upload_file.h"

// DEBUG_TEST_HOOK_WS_ACKSTALL_FORCE_RECONNECT (ack-stall watchdog firing counter for tests).
#include "mega/testhooks.h"

// Full UploadEngine::Impl definition. Needed by bodies that dereference
// mImpl-> / impl. members (refreshPools posts captures using
// mImpl->{client.wsPostToClientThread, instanceId, stopping};
// applyRefreshedUrls reads impl.poolConnectionLimit() via mImpl;
// retireUnusedPinnedPools / checkPools read impl.currentTime).
// `mega/transfer/ws/wsupload_engine.h` transitively pulls in
// `mega/transfer/ws/ws_upload_file.h` and `mega/megaapp.h` so the inline Impl
// bodies see MegaApp complete; the explicit `ws_upload_file.h` include above
// is kept for clarity (same pattern used in ws_conn.cpp).
#include "mega/transfer/ws/wsupload_engine.h"

#include "mega/commands_ws.h" // CommandUSCForWsUpload (refreshPools)
#include "mega/logging.h"
#include "mega/megaclient.h" // MegaClient::wsPostToClientThread / queueCommand / wsEngine

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <curl/curl.h>

namespace mega
{
namespace ws
{

// ---------- Pool manager (USC refresh + cURL multi) ----------
// struct WsPoolMgr is declared in include/mega/transfer/ws/ws_pool_mgr.h.
// All method bodies except curlIO / ensurePinnedPool (ws_curl.cpp) live here.

WsPoolMgr::WsPoolMgr()
{
    curlm = curl_multi_init();
}

WsPoolMgr::~WsPoolMgr()
{
    if (curlm)
    {
        curl_multi_cleanup(curlm);
        curlm = nullptr;
    }
}

void WsPoolMgr::bumpLastNetRead(const dstime now)
{
    if (SteadyTime::difference(now, mLastNetRead) > 0)
        mLastNetRead = now;
}

void WsPoolMgr::bumpAllPools(const dstime now)
{
    for (auto& p: mPools)
    {
        p->mLastActive = now;
        p->mLastServerResponse = now;
    }
}

void WsPoolMgr::markPoolRetiring(WsPool& pool)
{
    if (pool.mRetiring)
    {
        return;
    }

    pool.mRetiring = true;
    pool.setPoolNumConn(0);
}

bool WsPoolMgr::poolHasNoWork(const WsPool& pool) const
{
    return (pool.mNumPoolFiles == 0) && (pool.mUploadingFile == nullptr) &&
           (pool.mNumChunksInFlight == 0) && pool.mToResend.empty();
}

void WsPoolMgr::cleanupRetiringPools()
{
    for (std::size_t i = mPools.size(); i-- > 0;)
    {
        if (mPools[i] && mPools[i]->mRetiring && !mPools[i]->stillActive())
        {
            LOG_info << "WsUpload: closing idle pool " << i << " (" << mPools[i]->mUrl << ")";
#ifndef NDEBUG
            // N4 fix: fold this pool's cumulative test-stats into the persistent accumulator
            // BEFORE erasing it, so getWsUploadStatsForTesting's aggregate (esp.
            // maxConnectionsWithInFlightSeen, the config==used proof) survives the refresh
            // that retired it. Same #ifndef NDEBUG gate as addWsUploadStatsForTesting.
            mPools[i]->addWsUploadStatsForTesting(mRetiredPoolStats);
#endif
            mPools.erase(mPools.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

void WsPoolMgr::applyRefreshBackoff(Error e)
{
    ++mRefreshFailCount;
    // Protects the mRefreshFailCount - 1 expression below from wrap-around if a future
    // caller ever routes here without the increment above.
    assert(mRefreshFailCount > 0 && "mRefreshFailCount must be positive at this point");
    const unsigned count = mRefreshFailCount - 1;
    const unsigned exponent = std::min<unsigned>(count, 6);
    const dstime baseDelay = secondsToDs(10); // 10 seconds
    const dstime maxDelay = secondsToDs(10 * 60); // 10 minutes
    dstime backoff = baseDelay * (static_cast<dstime>(1) << exponent);
    if (backoff > maxDelay)
    {
        backoff = maxDelay;
    }
    mNextRefreshAttempt = SteadyTime::ds() + backoff;

    LOG_warn << "[WsPoolMgr::refreshPools] USC command failed: " << e
             << " [poolMgr=" << this << "]";
}

// ========== WsPoolMgr methods coupled to UploadEngine::Impl / WsUploadFile ==========
// Methods coupled to UploadEngine::Impl / WsUploadFile. Each method either
// dereferences `mImpl->X` / `impl.X` (needs the full `UploadEngine::Impl`
// from wsupload_engine.h) or touches `WsUploadFile` members (needs the full
// class from ws_upload_file.h). Both headers are included above.

bool WsPoolMgr::pinnedPoolHasReference(const WsPool& pool, const UploadEngine::Impl& impl) const
{
    for (const auto& entry: impl.files)
    {
        const auto& uf = entry.second;
        if (!uf)
        {
            continue;
        }

        if (uf->mPool == &pool || uf->sessionUrlHint() == pool.mUrl)
        {
            return true;
        }
    }

    return false;
}

void WsPoolMgr::retireUnusedPinnedPools(UploadEngine::Impl& impl)
{
    for (std::size_t i = mPools.size(); i-- > 0;)
    {
        WsPool* const pool = mPools[i].get();
        if (!pool || !pool->mPinned || pool->mRetiring)
        {
            continue;
        }

        const bool hasReference = pinnedPoolHasReference(*pool, impl);
        const bool hasNoWork = poolHasNoWork(*pool);
        const bool idleLongEnough =
            SteadyTime::difference(impl.currentTime, pool->mLastActive) > POOLCONNKEEPALIVE;

        if (!hasReference && hasNoWork && idleLongEnough)
        {
            markPoolRetiring(*pool);
        }
    }
}

void WsPoolMgr::checkPools(UploadEngine::Impl& impl)
{
    // update last net read from pools
    for (std::size_t i = mPools.size(); i-- > 0;)
    {
        if (SteadyTime::difference(mPools[i]->mLastServerResponse, mLastNetRead) > 0)
            mLastNetRead = mPools[i]->mLastServerResponse;
    }

    // close idle retiring pools
    retireUnusedPinnedPools(impl);

    cleanupRetiringPools();

    // trim connections / refresh stale or stalled pools
    for (std::size_t i = mPools.size(); i-- > 0;)
    {
        WsPool* const pool = mPools[i].get();
        if (!pool)
        {
            continue;
        }

        const bool hasPoolWork = pool->mNumPoolFiles || pool->mUploadingFile ||
                                 pool->mNumChunksInFlight || !pool->mToResend.empty();
        const bool pinnedHasReference = pool->mPinned && pinnedPoolHasReference(*pool, impl);
        const bool shouldScaleUp = !pool->mRetiring && (hasPoolWork || pinnedHasReference);
        // Connection-count ceiling: a DATASET pool (>= 2 bound files, sticky across dispatcher
        // trickle-feed flaps via mDatasetSeen) gets the dataset limit unconditionally (the WS
        // engine cannot see TCP-absorbed loss, so that bump is NOT loss-gated); a lone-small-
        // file pool widens toward kLossBoostedConnLimit only after observing a REAL drop
        // (mLossObserved). Clean single-file pools return poolConnectionLimit() -> byte-
        // identical scale-up.
        unsigned char targetConnLimit = pool->lossBoostedConnLimitLocked(impl);

        // Global concurrency ceiling (A24): bound the SUM of boosted connections across pools
        // so several concurrent size-class pools cannot each reach K and blow the RSS budget.
        // Reads LIVE per-pool counts, so it is cross-tick correct (a pool that scaled up on a
        // prior tick is already counted in otherConns). Only ever clamps a boost ABOVE the
        // default; the default/base path is untouched. Inert for the single active pool of the
        // uniform proof cell (one pool << ceiling) and byte-identical when both bump knobs are
        // off (targetConnLimit == poolConnectionLimit() then, so this block is skipped).
        if (targetConnLimit > impl.poolConnectionLimit())
        {
            const unsigned ceiling = impl.lossBoostedGlobalConnCeiling();
            unsigned otherConns = 0;
            for (const auto& p: mPools)
            {
                if (p && p.get() != pool && !p->mPinned)
                    otherConns += p->mNumberOfConnections;
            }
            const unsigned allowed = (ceiling > otherConns) ? (ceiling - otherConns) : 0u;
            const unsigned floorLimit = std::max<unsigned>(pool->mNumberOfConnections,
                                                           impl.poolConnectionLimit());
            const unsigned clamped =
                std::max<unsigned>(floorLimit, std::min<unsigned>(targetConnLimit, allowed));
            targetConnLimit = static_cast<unsigned char>(std::min<unsigned>(clamped, 255u));
        }

#ifndef NDEBUG
        pool->recordWsUploadStatsSampleLocked(impl);
#endif

        // [WsConnTelemetry] (SDK-5360 fu8 S8, Goal-0 gap #3): periodic per-pool conn-trajectory
        // line, OUTSIDE the gate branch so it emits on BOTH gate states -- the S7 GATE=0/K32
        // bench arm was unobservable (no [GoodputGate] lines and the 30-min guardrail kill
        // skipped the teardown stats), so its conn count was pure inference. Guarded to pools
        // with work so idle pools do not spam the trace; 0 = off.
        if (impl.mConnTelemetryMs &&
            (pool->mNumPoolFiles || pool->mNumChunksInFlight || pool->mUploadingFile) &&
            SteadyTime::difference(impl.currentTime, pool->mGateTelemetryNextDs) >= 0)
        {
            unsigned tOpen = 0;
            unsigned tBp = 0;
            pool->countOpenAndBackpressuredLocked(tOpen, tBp);
            LOG_debug << "[WsConnTelemetry] pool=" << static_cast<const void*>(pool)
                      << " files=" << pool->mNumPoolFiles
                      << " gate=" << (impl.mDatasetConnGate ? 1 : 0)
                      << " conns=" << static_cast<unsigned>(pool->mNumberOfConnections)
                      << " open=" << tOpen << " bp=" << tBp
                      << " inflight=" << pool->mNumChunksInFlight
                      << " resend=" << pool->mToResend.size()
                      << " confirmedBytes=" << pool->mConfirmedBytesTotal;
            pool->mGateTelemetryNextDs = impl.currentTime + impl.connTelemetryDs();
        }

        // Scale-up: goodput-saturation GATE v2 (SDK-5360 QCT-K) or the pre-gate unconditional
        // jump. targetConnLimit is the CEILING (base pool limit max'd with any active boost,
        // already global-ceiling-clamped above). Two regimes:
        //   * Gate OFF (MEGA_WS_DATASET_CONN_GATE=0) OR no boost and not above base: JUMP
        //     straight to targetConnLimit -- byte-identical to the pre-gate scale-up (the normal
        //     cold-start rise to the base default, and the unconditional dataset bump when the
        //     gate is off).
        //   * Gate ON AND (boost active OR the pool is parked ABOVE base): run the QCT-K
        //     controller. The above-base arm is the S8 FLAW-1 fix -- without it a pool at the
        //     ceiling (the bad-net steady state) or one whose boost lapsed was never revisited,
        //     so "then trim" was unreachable and extra conns stranded until pool retirement. The
        //     ceiling passed in is floored at the live count so a lapsed boost cannot present a
        //     degenerate ceiling below the current level. The pool is first brought to the base
        //     default un-ramped (the controller governs ONLY the region above the base; the base
        //     rise stays immediate).
        if (shouldScaleUp)
        {
            const bool boostActive = (targetConnLimit > impl.poolConnectionLimit());
            const bool aboveBase = (pool->mNumberOfConnections > impl.poolConnectionLimit());
            if (impl.mDatasetConnGate && (boostActive || aboveBase))
            {
                if (pool->mNumberOfConnections < impl.poolConnectionLimit())
                {
                    pool->setPoolNumConn(impl.poolConnectionLimit());
                }
                pool->runGoodputGateLocked(
                    impl,
                    std::max<unsigned char>(targetConnLimit, pool->mNumberOfConnections));
            }
            else if (pool->mNumberOfConnections < targetConnLimit)
            {
                pool->setPoolNumConn(targetConnLimit);
            }
        }

        if (pool->mNumberOfConnections > 1 && !shouldScaleUp &&
            SteadyTime::difference(impl.currentTime, pool->mLastActive) > POOLCONNKEEPALIVE)
        {
            pool->setPoolNumConn(1);
        }

        if (SteadyTime::difference(impl.currentTime, pool->mPoolCreationTime) > POOLFRESHNESS)
            refreshPools();

        if ((pool->mUploadingFile || pool->mNumChunksInFlight || !pool->mToResend.empty()) &&
            SteadyTime::difference(impl.currentTime, pool->mLastActive) > SERVERTIMEOUT)
            refreshPools();

        // Ack-stall watchdog (SDK-5360 fu8 Session 6): force-reconnect a silently-hung OPEN
        // connection whose server acks have gone stale past ACKSTALLTIMEOUT while it still
        // holds in-flight chunks. Keys on the per-conn last-inbound-frame stamp (TRUE server
        // liveness), NOT pool->mLastActive — which our own chunk-prep sends bump, so a conn we
        // keep writing to but that never acks would otherwise stay undetected (the boss's
        // "stuck transfer" case: slow/lossy link, server silent, no TCP drop). Skips pinned and
        // retiring pools, and active server-throttle windows (the server is responsive then).
        // WsConn is worker-owned, so we SIGNAL via mForceReconnect (honoured beside the worker's
        // disconnectEpoch check) rather than closing here; the worker reuses closeWS -> onclose
        // -> retryChunksOnTheWireLocked so un-acked chunks are re-queued (acked bytes preserved
        // via mAckedIntervals). checkPools runs under uploadMutex, so pool->mConns is stable and
        // each conn's atomic stamp reads cleanly.
        if (impl.mAckStallWatchdog && !pool->mPinned && !pool->mRetiring &&
            !pool->throttledByServer())
        {
            const dstime ackStallTimeout = impl.ackStallTimeoutDs();
            for (WsConn* const conn: pool->mConns)
            {
                if (!conn ||
                    conn->readyState.load(std::memory_order_relaxed) !=
                        WsConn::ReadyState::OPEN ||
                    conn->mChunksInFlight.empty())
                {
                    continue;
                }
                const dstime lastInbound =
                    conn->mLastInboundFrameDs.load(std::memory_order_relaxed);
                const dstime lastSend =
                    conn->mLastSendProgressDs.load(std::memory_order_relaxed);
                // A conn is HUNG only if BOTH server acks AND our own send progress have gone
                // silent past the window: no acks (server not responding) AND no bytes accepted
                // by curl_ws_send (send buffer wedged). A legitimately SLOW conn (rate-limited
                // via setmaxuploadspeed, low-bandwidth, or draining a pause) keeps stamping
                // lastSend and/or lastInbound, so it is NOT force-reconnected. This progress-guard
                // fixes the RepeatedPauseResumeMixedPools regression + the loss20 over-fire.
                if (lastInbound && lastSend &&
                    SteadyTime::difference(impl.currentTime, lastInbound) > ackStallTimeout &&
                    SteadyTime::difference(impl.currentTime, lastSend) > ackStallTimeout &&
                    !conn->mForceReconnect.load(std::memory_order_relaxed))
                {
                    LOG_warn << "[WsPoolMgr::checkPools] ack-stall: OPEN conn with "
                             << conn->mChunksInFlight.size()
                             << " in-flight chunk(s) but no server frame AND no send progress for "
                                "> ackStallTimeout ("
                             << ackStallTimeout << "ds); force-reconnecting [pool = " << pool
                             << "] [conn = " << conn << "]";
                    conn->mForceReconnect.store(true, std::memory_order_relaxed);
                    // notifyWorkersLocked() (NOT notifyWorkers()): checkPools already holds
                    // uploadMutex, and notifyWorkers() re-acquires it -> self-deadlock on the
                    // engine thread (froze the whole WS engine at the first watchdog fire).
                    impl.notifyWorkersLocked();
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                    DEBUG_TEST_HOOK_WS_ACKSTALL_FORCE_RECONNECT(conn, pool);
#endif
                }
            }
        }

        // Tail-completion watchdog (SDK-5360 fu8 Session 9). Companion to the ack-stall guard
        // for the state it structurally cannot see: a file whose bytes are ALL server-confirmed
        // but whose one-shot completion frame (upload token) was lost — the pool then idles at
        // files>=1 / inflight=0 / resend=0 forever (S9 P6 evidence: 55-min wedge after a
        // transient endpoint handshake outage; no conn holds in-flight chunks, so the ack-stall
        // loop above never fires). Detect persistence past tailCompletionTimeoutDs() and fail
        // the file for retry through the SAME recovery the sustained-handshake escalation uses
        // (purge + markFailedForRetry + onFail(API_EAGAIN, Retryable)); the retry re-uploads
        // and re-fetches a completion. Gated by mAckStallWatchdog so watchdog-off A/B arms
        // (MEGA_WS_ACKSTALL_WATCHDOG=0, e.g. the K32 comparability arm) stay byte-identical.
        // Runs under uploadMutex (checkPools contract), matching every touched field's lock
        // discipline. Skips retiring pools (file migrates with the refresh) and active
        // server-throttle windows (completion may legitimately be deferred there).
        if (impl.mAckStallWatchdog && !pool->mRetiring && !pool->throttledByServer() &&
            pool->mNumChunksInFlight == 0 && pool->mToResend.empty())
        {
            WsUploadFile* wedged = nullptr;
            for (const auto& kv: impl.fileByNo)
            {
                WsUploadFile* const f = kv.second;
                if (f && f->mPool == pool && !f->paused() && f->completionWedgeCandidateLocked())
                {
                    wedged = f;
                    break;
                }
            }
            if (!wedged)
            {
                pool->mTailWedgeFileno = 0;
                pool->mTailWedgeSinceDs = 0;
            }
            else if (pool->mTailWedgeFileno != wedged->fileno())
            {
                pool->mTailWedgeFileno = wedged->fileno();
                pool->mTailWedgeSinceDs = impl.currentTime;
            }
            else if (SteadyTime::difference(impl.currentTime, pool->mTailWedgeSinceDs) >
                     impl.tailCompletionTimeoutDs())
            {
                LOG_warn << "[WsPoolMgr::checkPools] tail-completion wedge: file "
                         << wedged->fileno() << " has all " << wedged->size()
                         << " bytes server-confirmed but no completion for > "
                         << impl.tailCompletionTimeoutDs()
                         << "ds with an idle pool; failing for retry [pool = " << pool << "]";
                const std::uint32_t fileno = wedged->fileno();
                pool->purgeFileLocked(fileno);
                wedged->markFailedForRetry(0);
                wedged->unsetPool();
                if (pool->mUploadingFile == wedged)
                {
                    pool->clearUploadingFileLocked();
                }
                pool->mUFTQversion = impl.queueVersion.load(std::memory_order_relaxed);
                pool->mTailWedgeFileno = 0;
                pool->mTailWedgeSinceDs = 0;
                if (impl.mCb.onFail)
                {
                    impl.mCb.onFail(wedged->transfer(),
                                    API_EAGAIN,
                                    0,
                                    UploadEngine::FailureDisposition::Retryable);
                }
                impl.notifyWorkersLocked();
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                DEBUG_TEST_HOOK_WS_TAILCOMPLETION_RECOVERY(fileno, pool);
#endif
            }
        }
    }

    // throughput display tick (server-acked)
    for (auto* uf: mActiveFiles)
        uf->mClientActiveFilesTick = false;
    mActiveFiles.clear();
}

// refreshPools posts a series of lambdas to the client thread via wsPostToClientThread().
// Those lambdas capture `MegaClient&` by reference and rely on the SDK-wide contract that
// MegaClient destroys its UploadEngine synchronously on the client thread during teardown
// (~MegaClient), so every queued work item sees a live MegaClient. The `clearRefreshing`
// closure additionally re-validates wsEngine() identity under uploadMutex to guard against
// engine-replacement mid-flight (race scenario: an engine instance is replaced while a
// refresh-pools USC reply is in-flight).
void WsPoolMgr::refreshPools()
{
    if (!mImpl)
        return;
    if (mImpl->stopping())
        return;

    const dstime now = SteadyTime::ds();

    // Distress/refresh-churn throttle (mDistressRefreshThrottle, MEGA_WS_REFRESH_THROTTLE).
    // Every refresh trigger (SERVERTIMEOUT periodic in checkPools, server event=5 Distress
    // in ws_conn.cpp, worker handshake-fail in ws_pool.cpp) funnels through here, so one
    // success-side minimum-spacing gate coalesces a storm of triggers into at most one
    // refresh per WS_REFRESH_MIN_INTERVAL. Correctness-neutral: a real URL change still
    // applies on the next admitted tick (<= interval later), and applyRefreshedUrls
    // preserves a still-valid URL by exact match, so a throttled-then-admitted refresh is
    // non-destructive when nothing changed. Liveness is preserved because a genuinely-dead
    // endpoint is independently backstopped by the handshake-failure escalation gate
    // (HANDSHAKEFAILTIMEOUT=60s in ws_pool.cpp), and WS_REFRESH_MIN_INTERVAL (30s) sits
    // strictly inside [SERVERTIMEOUT 20s, HANDSHAKEFAILTIMEOUT 60s] so the escalation path
    // is unaffected. On a clean link no trigger fires, so this gate never engages.
    if (mImpl->mDistressRefreshThrottle && mLastRefreshStartedDs &&
        SteadyTime::difference(now, mLastRefreshStartedDs) < WS_REFRESH_MIN_INTERVAL)
    {
        return;
    }

    if (now < mNextRefreshAttempt)
    {
        return;
    }

    if (mRefreshing.exchange(true))
    {
        return;
    }
    // Stamp at launch (not completion) so a slow USC round-trip cannot let a second
    // refresh slip in behind it. The fail-path exponential backoff (mNextRefreshAttempt)
    // still composes on top; whichever gate is longer wins.
    mLastRefreshStartedDs = now;
    // Count ADMITTED refreshes (past the throttle/backoff/latch gates), i.e. the ones that
    // actually launch a USC round-trip and can replace pools. This is the "refresh volume"
    // that W1/N4 correlated with peak RSS; surfaced per run so the bench can gate HR54 on
    // churn-free reps and quantify the churn->RSS relationship (ledger N7).
    ++mRefreshPoolsCount;

    const auto engineId = mImpl->instanceId();

    auto validateEngine = [](MegaClient& client, const std::uint64_t id) -> UploadEngine*
    {
        auto* engine = client.wsEngine();
        if (!engine || engine->instanceId() != id || engine->isStopping())
        {
            return nullptr;
        }
        return engine;
    };

    auto clearRefreshing = [](MegaClient& client, const std::uint64_t id)
    {
        if (auto* engine = client.wsEngine())
            engine->clearRefreshLatchForInstance(id);
    };

    mImpl->client.wsPostToClientThread(
        [engineId, validateEngine, clearRefreshing](MegaClient& client, TransferDbCommitter&)
        {
            if (!validateEngine(client, engineId))
            {
                clearRefreshing(client, engineId);
                return;
            }

            // Queue lockless USC command using the RequestDispatcher. This avoids blocking on
            // the main client-server channel when lockless channels are enabled.
            client.queueCommand(new CommandUSCForWsUpload(
                client,
                [&client, engineId, validateEngine, clearRefreshing](
                    Error e,
                    std::vector<std::pair<std::string, m_off_t>>&& sizeClasses)
                {
                    auto* currentEngine = validateEngine(client, engineId);
                    if (!currentEngine)
                    {
                        clearRefreshing(client, engineId);
                        return;
                    }

                    currentEngine->applyRefreshResultForInstance(engineId,
                                                                 e,
                                                                 std::move(sizeClasses));
                }));
        });
}

void WsPoolMgr::applyRefreshedUrls(std::vector<std::pair<std::string, m_off_t>> apiSizeClasses)
{
    if (apiSizeClasses.empty())
    {
        LOG_warn << "WsUpload: USC returned no upload pools";
        return;
    }

    const dstime now = SteadyTime::ds();

    // Mark all currently-active pools as retiring; we'll unretire those that still match.
    for (std::size_t i = 0; i < mPools.size(); ++i)
    {
        if (!mPools[i]->mPinned)
        {
            mPools[i]->mRetiring = true;
        }
    }

    for (std::size_t i = 0; i < apiSizeClasses.size(); ++i)
    {
        const auto& refreshed = apiSizeClasses[i];
        const auto activateMatchedPool = [this, i](const std::size_t matchedIndex)
        {
            if (matchedIndex != i)
            {
                std::swap(mPools[i], mPools[matchedIndex]);
            }
            mPools[i]->mRetiring = false;
        };

        bool matched = false;

        // Prefer exact endpoint identity to preserve in-flight state whenever possible.
        for (std::size_t j = mPools.size(); j-- > 0;)
        {
            if (mPools[j]->mPinned)
            {
                continue;
            }
            if (mPools[j]->mMaxFileSize == refreshed.second && mPools[j]->mUrl == refreshed.first)
            {
                mPools[j]->mPoolCreationTime = now;
                activateMatchedPool(j);
                matched = true;
                break;
            }
        }

        // Host+size fallback is only safe for a fully idle pool. Active pools keep retiring
        // and a new pool is created for the refreshed endpoint.
        if (!matched)
        {
            for (std::size_t j = mPools.size(); j-- > 0;)
            {
                WsPool* const pool = mPools[j].get();
                if (!pool || pool->mPinned || !pool->sameHostMaxSize(refreshed))
                {
                    continue;
                }

                const bool canRetarget =
                    poolHasNoWork(*pool) && pool->mConns.empty() &&
                    pool->mActiveThreads.empty() && pool->mExitingThreads.empty();
                if (!canRetarget)
                {
                    continue;
                }

                if (pool->mUrl != refreshed.first)
                {
                    LOG_info << "WsUpload: retargeting idle pool URL"
                             << " [old=" << pool->mUrl << "] [new=" << refreshed.first << "]";
                    pool->mUrl = refreshed.first;
                }

                pool->mPoolCreationTime = now;
                activateMatchedPool(j);
                matched = true;
                break;
            }
        }

        if (!matched)
        {
            mPools.insert(mPools.begin() + static_cast<std::ptrdiff_t>(i),
                          std::make_unique<WsPool>(refreshed,
                                                   i ? apiSizeClasses[i - 1].second : 0,
                                                   mImpl,
                                                   mImpl->poolConnectionLimit()));
        }
    }

    bumpAllPools(now);
    LOG_info << "WsUpload: refreshed pools (" << apiSizeClasses.size() << " size classes)";
}

// Forwarding helper for ws_curl.cpp (and now any sibling TU). Defined here
// because UploadEngine::Impl is included in this TU via wsupload_engine.h.
unsigned char WsPoolMgr::pinnedPoolConnectionLimit() const
{
    return mImpl->poolConnectionLimit();
}

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD
