/**
 * @file src/transfer/ws/ws_pool_mgr.cpp
 * @brief WsPoolMgr lifecycle bodies — split out of wsupload.cpp (fu7-16
 *        Goal 2.Step1). Hosts the WsPoolMgr ctor/dtor plus the simple
 *        WsPool-only bookkeeping methods that do not need the full
 *        UploadEngine::Impl definition.
 *
 *        Methods that touch Impl members (refreshPools / applyRefreshedUrls /
 *        retireUnusedPinnedPools / pinnedPoolConnectionLimit) or WsUploadFile
 *        members (pinnedPoolHasReference / checkPools) remain in
 *        src/transfer/ws/wsupload.cpp, because pulling wsupload_engine.h into
 *        a sibling TU transitively requires the WsUploadFile class to be a
 *        complete type — and WsUploadFile is TU-local to wsupload.cpp until
 *        fu7-16 Goal 2.Step2. See domain_coupling_map.md §5 SS-1.
 *
 *        WsPoolMgr::curlIO and WsPoolMgr::ensurePinnedPool stay in the
 *        sibling TU src/transfer/ws/ws_curl.cpp (fu7-14 G2.a-β).
 *
 *        Relies on src/transfer/ws/wsupload_internal.h for the WsPool /
 *        SteadyTime / WSUPLOAD_TRACE cluster.
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

#include "mega/logging.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <curl/curl.h>

namespace mega
{
namespace ws
{

// ---------- Pool manager (USC refresh + cURL multi) ----------
// struct WsPoolMgr is declared in include/mega/transfer/ws/ws_pool_mgr.h.
// The simple ctor/dtor + bumpLastNetRead / bumpAllPools / markPoolRetiring /
// poolHasNoWork / cleanupRetiringPools / applyRefreshBackoff live here.
// Other method bodies remain in wsupload.cpp (Impl/WsUploadFile coupling).

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

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD
