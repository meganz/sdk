/**
 * @file src/transfer/ws/ws_pool_mgr.cpp
 * @brief WsPoolMgr lifecycle bodies — split out of wsupload.cpp in fu7-16
 *        Goal 2.Step1 (ctor/dtor + simple WsPool-only bookkeeping) and
 *        Goal 2.Step4 (the remaining Impl/WsUploadFile-coupled methods that
 *        Step 1 deferred while WsUploadFile was still TU-local).
 *
 *        Bodies now hosted here:
 *
 *          - ctor / dtor (Step 1)
 *          - bumpLastNetRead / bumpAllPools (Step 1)
 *          - markPoolRetiring / poolHasNoWork / cleanupRetiringPools (Step 1)
 *          - applyRefreshBackoff (Step 1)
 *          - pinnedPoolHasReference / retireUnusedPinnedPools (Step 4)
 *          - checkPools (Step 4)
 *          - refreshPools (Step 4)
 *          - applyRefreshedUrls (Step 4)
 *          - pinnedPoolConnectionLimit (Step 4)
 *
 *        WsPoolMgr::curlIO and WsPoolMgr::ensurePinnedPool live in the sibling
 *        TU src/transfer/ws/ws_curl.cpp (fu7-14 G2.a-β).
 *
 *        Includes:
 *          - mega/transfer/ws/ws_pool_mgr.h for the WsPoolMgr declaration.
 *          - wsupload_internal.h for the WsPool / SteadyTime / WSUPLOAD_TRACE
 *            cluster.
 *          - wsupload_engine.h for the full UploadEngine::Impl definition
 *            (needed by the Step-4 methods that dereference Impl members).
 *          - ws_upload_file.h for the WsUploadFile complete type (used by
 *            pinnedPoolHasReference, retireUnusedPinnedPools, refreshPools'
 *            wsPostToClientThread captures and applyRefreshedUrls via
 *            poolHasNoWork).
 *          - mega/commands_ws.h for CommandUSCForWsUpload used by refreshPools.
 *
 * (c) 2026 by Mega Limited, Auckland, New Zealand
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
// WSUPLOAD_TRACE, etc.). Private header under src/, not include/; relative
// include since src/ is not on the SDKlib include path.
#include "wsupload_internal.h"

// Full WsUploadFile definition. Used by Goal 2.Step4 bodies below
// (pinnedPoolHasReference dereferences uf->mPool / uf->sessionUrlHint(),
// retireUnusedPinnedPools through pinnedPoolHasReference, applyRefreshedUrls
// indirectly via poolHasNoWork → pool.mUploadingFile).
#include "ws_upload_file.h"

// Full UploadEngine::Impl definition. Needed by Goal 2.Step4 bodies that
// dereference mImpl-> / impl. members (refreshPools posts captures using
// mImpl->{client.wsPostToClientThread, instanceId, stopping};
// applyRefreshedUrls reads impl.poolConnectionLimit() via mImpl;
// retireUnusedPinnedPools / checkPools read impl.currentTime).
// `wsupload_engine.h` transitively pulls in `ws_upload_file.h` and
// `mega/megaapp.h` so the inline Impl bodies see MegaApp complete; the
// explicit `ws_upload_file.h` include above is kept for clarity (same
// pattern used in ws_conn.cpp).
#include "wsupload_engine.h"

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
// Promoted out of src/transfer/ws/wsupload.cpp in fu7-16 Goal 2.Step4. Each
// method either dereferences `mImpl->X` / `impl.X` (needs the full
// `UploadEngine::Impl` from wsupload_engine.h) or touches `WsUploadFile`
// members (needs the full class from ws_upload_file.h). Both headers are
// included above.

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
        const unsigned char targetConnLimit = impl.poolConnectionLimit();

#ifndef NDEBUG
        pool->recordWsUploadStatsSampleLocked(impl);
#endif

        if (shouldScaleUp && pool->mNumberOfConnections < targetConnLimit)
        {
            pool->setPoolNumConn(targetConnLimit);
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
    }

    // throughput display tick (server-acked)
    for (auto* uf: mActiveFiles)
        uf->mClientActiveFilesTick = false;
    mActiveFiles.clear();
}

// refreshPools posts a series of lambdas to the client thread via wsPostToClientThread().
// Those lambdas capture `MegaClient&` by reference and rely on the SDK-wide contract that
// MegaClient destroys its UploadEngine synchronously on the client thread during teardown
// (~MegaClient), so every queued work item sees a live MegaClient. NF-4's clearRefreshing
// closure additionally re-validates wsEngine() identity under uploadMutex to guard against
// engine-replacement mid-flight.
void WsPoolMgr::refreshPools()
{
    if (!mImpl)
        return;
    if (mImpl->stopping())
        return;

    const dstime now = SteadyTime::ds();
    if (now < mNextRefreshAttempt)
    {
        return;
    }

    if (mRefreshing.exchange(true))
    {
        return;
    }

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
