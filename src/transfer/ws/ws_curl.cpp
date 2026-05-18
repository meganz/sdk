/**
 * @file src/transfer/ws/ws_curl.cpp
 * @brief WsPoolMgr cURL I/O slice — split out of wsupload.cpp to keep the
 *        manager-thread cURL drain + pinned-pool bootstrap in a focused
 *        translation unit. Only the bodies of WsPoolMgr::curlIO() and
 *        WsPoolMgr::ensurePinnedPool() live here; everything else (declarations,
 *        sibling helpers, UploadEngine::Impl) stays where it already was.
 *
 *        Relies on include/mega/transfer/ws/wsupload_internal.h for WsPool,
 *        SteadyTime, ScopedUnlock, WSUPLOAD_CURL_MULTI_POLL_MS and
 *        WSUPLOAD_TRACE; uses the WsPoolMgr::pinnedPoolConnectionLimit()
 *        forwarding helper (body in src/transfer/ws/wsupload.cpp) to query the
 *        pool-connection limit without pulling in UploadEngine::Impl's full
 *        definition.
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
// ScopedUnlock, WSUPLOAD_CURL_MULTI_POLL_MS, WSUPLOAD_TRACE). SDK-internal
// architecture header (fu7-17 G1.a relocated to include/mega/transfer/ws/).
#include "mega/transfer/ws/wsupload_internal.h"

#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <curl/curl.h>

namespace mega
{
namespace ws
{

void WsPoolMgr::curlIO(std::unique_lock<std::mutex>& lk)
{
    // Refresh runs via CommandUSCForWsUpload + queueCommand + wsPostToClientThread
    // (see refreshPools()); no curl-easy handles are ever added to `curlm` on the live
    // path, so the perform/info-read drain here is a no-op in practice. The shell is
    // kept to preserve the manager-thread cadence and to allow a future live consumer
    // to slot handles in without reintroducing scaffolding.
    int still_running = 0, msgs_left = 0;
    curl_multi_perform(curlm, &still_running);

    CURLMsg* msg;
    while ((msg = curl_multi_info_read(curlm, &msgs_left)) != nullptr)
    {
        // Drain the message queue; no handlers are registered on `curlm` today.
        (void)msg;
    }

    // Don't hold the engine mutex while blocking in curl I/O.
    {
        ScopedUnlock unlock(lk);
        (void)curl_multi_poll(curlm, nullptr, 0, WSUPLOAD_CURL_MULTI_POLL_MS, nullptr);
    }
}

void WsPoolMgr::ensurePinnedPool(const std::string& url)
{
    if (!mImpl || url.empty())
    {
        return;
    }

    for (const auto& pool: mPools)
    {
        if (pool && pool->mPinned && !pool->mRetiring && pool->mUrl == url)
        {
            return;
        }
    }

    // Create a dedicated pool that will only serve transfers pinned to this session URL.
    auto pool = std::make_unique<WsPool>(std::make_pair(url, static_cast<m_off_t>(0)),
                                         0,
                                         mImpl,
                                         pinnedPoolConnectionLimit());
    pool->mPinned = true;
    mPools.emplace_back(std::move(pool));

    bumpAllPools(SteadyTime::ds());
}

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD
