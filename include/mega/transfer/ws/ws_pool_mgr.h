/**
 * @file mega/transfer/ws/ws_pool_mgr.h
 * @brief Declaration of WsPoolMgr — USC refresh + cURL multi pool manager
 *        used by the websocket-upload engine.
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

#ifndef MEGA_TRANSFER_WS_WS_POOL_MGR_H
#define MEGA_TRANSFER_WS_WS_POOL_MGR_H 1

#ifdef MEGA_USE_WSUPLOAD

#include "mega/types.h"
#include "mega/wsupload.h" // For UploadEngine

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <curl/curl.h>

namespace mega
{
namespace ws
{

struct WsPool;
class WsUploadFile;

// Time-domain constants and conversion helpers shared between wsupload.cpp
// and the WsPoolMgr split (future ws_curl.cpp). Kept in this header rather
// than wsupload.cpp so that WsPoolMgr's member-initialiser constexprs
// (POOLCONNKEEPALIVE = secondsToDs(60), etc.) resolve from the header alone.
constexpr dstime kDsPerSecond = 10;
constexpr std::int64_t kMsPerDeciSecond = 100;

constexpr dstime secondsToDs(const std::int64_t seconds)
{
    return static_cast<dstime>(seconds * kDsPerSecond);
}

constexpr dstime msToDs(const std::int64_t ms)
{
    return static_cast<dstime>(ms / kMsPerDeciSecond);
}

constexpr std::int64_t dsToMs(const std::int64_t ds)
{
    return ds * kMsPerDeciSecond;
}

// Pool manager (USC refresh + cURL multi). Owned by UploadEngine::Impl; not
// part of the public SDK ABI. Forward-declared types (WsPool, WsUploadFile,
// UploadEngine::Impl) keep this header lightweight. Member function bodies
// are defined in src/transfer/ws/ws_pool_mgr.cpp and the sibling
// src/transfer/ws/ws_curl.cpp, which hosts WsPoolMgr::curlIO() and
// WsPoolMgr::ensurePinnedPool().
struct WsPoolMgr
{
    static constexpr dstime POOLCONNKEEPALIVE = secondsToDs(60);
    static constexpr dstime POOLFRESHNESS = secondsToDs(24 * 3600);
    const dstime SERVERTIMEOUT = secondsToDs(20);
    // Distress/refresh-churn throttle: minimum spacing between successful refreshPools()
    // launches (mDistressRefreshThrottle, MEGA_WS_REFRESH_THROTTLE). 30s is chosen to sit
    // strictly inside (SERVERTIMEOUT=20s, HANDSHAKEFAILTIMEOUT=60s): > SERVERTIMEOUT so the
    // periodic checkPools trigger cannot re-fire every tick, < HANDSHAKEFAILTIMEOUT so the
    // dead-endpoint escalation gate still acts as the liveness backstop.
    static constexpr dstime WS_REFRESH_MIN_INTERVAL = secondsToDs(30);

    CURLM* curlm = nullptr; // private multi for USC refresh
    UploadEngine::Impl* mImpl{nullptr}; // backpointer

    std::vector<std::unique_ptr<WsPool>> mPools;

    std::unordered_set<WsUploadFile*> mActiveFiles; // progress reporting
    dstime mLastNetRead{0};
    std::atomic_bool mRefreshing{false};
    dstime mNextRefreshAttempt{0};
    // When the last refresh round-trip was launched (success-side throttle, see
    // WS_REFRESH_MIN_INTERVAL). 0 = never launched. Written only on the manager thread
    // in refreshPools() (under uploadMutex via its callers); the throttle read there is
    // on the same thread, so no extra synchronisation is needed.
    dstime mLastRefreshStartedDs{0};
    unsigned mRefreshFailCount{0};

    // Cumulative refreshPools() invocations this engine-lifetime. Surfaced via
    // getWsUploadStatsForTesting so the bench can attribute peak RSS to refresh churn per run
    // (W1/N4, SDK-5360 fu8: RSS peak tracks refresh VOLUME, not conn count). Always-compiled
    // (a trivial counter); read on the client thread under uploadMutex, incremented on the
    // manager thread under the same lock.
    std::uint64_t mRefreshPoolsCount{0};
#ifndef NDEBUG
    // Persistent fold of retired pools' test-stats (SDK-5360 fu8 N4 fix). cleanupRetiringPools
    // folds each pool here BEFORE erasing it from mPools, so getWsUploadStatsForTesting's
    // aggregate SURVIVES a refreshPools()-driven pool replacement. Without this a refresh
    // shortly before the end-of-run snapshot zeroed the whole [WsUploadStats] block (the
    // counter=0 / stats-vanish artifact). Debug-only, same gate as the stats it accumulates.
    UploadEngine::WsUploadStatsForTesting mRetiredPoolStats;
#endif

    WsPoolMgr();
    ~WsPoolMgr();

    void curlIO(std::unique_lock<std::mutex>& lk);
    void checkPools(UploadEngine::Impl& impl);
    void refreshPools();
    void applyRefreshBackoff(Error e);

    // Shared tail for USC refresh responses (parsing can be done via string parsing or JSON).
    void applyRefreshedUrls(std::vector<std::pair<std::string, m_off_t>> urls);

    // Ensure a dedicated pool exists for a pinned (resumed) session URL.
    void ensurePinnedPool(const std::string& url);

    void markPoolRetiring(WsPool& pool);
    bool poolHasNoWork(const WsPool& pool) const;
    bool pinnedPoolHasReference(const WsPool& pool, const UploadEngine::Impl& impl) const;
    void retireUnusedPinnedPools(UploadEngine::Impl& impl);
    void cleanupRetiringPools();

    void bumpLastNetRead(const dstime now);
    void bumpAllPools(const dstime now);

    // Forwarding helper so sibling TUs (ws_curl.cpp) can query connection
    // limits without pulling in UploadEngine::Impl's full definition. Body
    // lives in src/transfer/ws/wsupload.cpp where Impl is visible.
    unsigned char pinnedPoolConnectionLimit() const;
};

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD

#endif // MEGA_TRANSFER_WS_WS_POOL_MGR_H
