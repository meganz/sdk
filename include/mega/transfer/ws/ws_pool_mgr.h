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

    CURLM* curlm = nullptr; // private multi for USC refresh
    UploadEngine::Impl* mImpl{nullptr}; // backpointer

    std::vector<std::unique_ptr<WsPool>> mPools;

    std::unordered_set<WsUploadFile*> mActiveFiles; // progress reporting
    dstime mLastNetRead{0};
    std::atomic_bool mRefreshing{false};
    dstime mNextRefreshAttempt{0};
    unsigned mRefreshFailCount{0};

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
