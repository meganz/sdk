/**
 * @file src/transfer/ws/wsupload.cpp
 * @brief Public UploadEngine facade — the pImpl forwarding shell that backs
 *        the `mega::ws::UploadEngine` declared in `mega/wsupload.h`. The TU is
 *        deliberately small after the multi-TU split:
 *
 *          - Anonymous-namespace TU-local helpers: detail::validateInboundFrame
 *            (declared in mega/wsupload.h), ChunkMap + g_chunkMap +
 *            chunkSizeAtPosition free wrapper, Debug-only steadyMs().
 *          - Out-of-class definitions of UploadEngine::Impl::clearRefreshLatchForInstance
 *            and UploadEngine::Impl::applyRefreshResultForInstance (kept here
 *            because they are tiny lock-guarded paste-throughs to
 *            poolMgr.applyRefreshBackoff / applyRefreshedUrls; moving them to
 *            the header would force a circular include with ws_pool_mgr.cpp).
 *          - UploadEngine public-facade pImpl forwarders (~25 methods, each
 *            1-5 lines).
 *          - `wsEnabled(const MegaClient&)` feature gate.
 *
 *        Larger bodies live in sibling TUs:
 *
 *          - include/mega/transfer/ws/wsupload_engine.h  — UploadEngine::Impl
 *            class.
 *          - src/transfer/ws/ws_pool_mgr.cpp    — WsPoolMgr method bodies.
 *          - src/transfer/ws/ws_curl.cpp        — WsPoolMgr::curlIO and
 *            ensurePinnedPool.
 *          - include/mega/transfer/ws/ws_upload_file.h + src/transfer/ws/
 *            ws_upload_file.cpp — WsUploadFile class.
 *          - src/transfer/ws/ws_conn.cpp        — WsConn + WsBuf::sendWS +
 *            WsPoolThread ctor + ChunkFingerprintMacUpdate::apply.
 *          - src/transfer/ws/ws_pool.cpp        — every WsPool method body
 *            including sendChunk + poolWorkerThread.
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

#include "mega/wsupload.h"

// File-internal types shared with sibling WS TUs (CRC32 trailer check used by
// detail::validateInboundFrame). SDK-internal architecture header.
#include "mega/transfer/ws/wsupload_internal.h"

// Full WsUploadFile definition — pulled in by wsupload_engine.h transitively,
// but listed explicitly for symmetry with the sibling TUs. Needed because the
// out-of-class Impl::clearRefreshLatchForInstance / applyRefreshResultForInstance
// bodies indirectly include it via wsupload_engine.h.
#include "mega/transfer/ws/ws_upload_file.h"

// Full UploadEngine::Impl definition. Needed for every pImpl forwarder body
// below (`pImpl->X`). Transitively pulls in mega/megaapp.h because the engine
// header is intentionally self-contained.
#include "mega/transfer/ws/wsupload_engine.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace mega
{
namespace ws
{

// Steady-clock millisecond helper used by Debug-only book-keeping in
// ws_pool.cpp (assignUploadingFileLocked / recordFirstByteSent etc.) and by
// optional in-engine timestamps. Defined here (not in the engine header)
// because the chrono dependency is internal to the wsupload.cpp TU stack.
#ifndef NDEBUG
std::uint64_t steadyMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
#endif

namespace detail
{

InboundFrameValidationResult validateInboundFrame(const char* msg, const int len)
{
    if (len < kMinInboundFrameBytes)
    {
        return InboundFrameValidationResult::TooShort;
    }

    std::uint32_t trailerCrc = 0;
    std::memcpy(&trailerCrc,
                msg + len - kInboundFrameTrailerCrcBytes,
                sizeof(trailerCrc));
    if (trailerCrc != CRC32::crc32b(msg, len - kInboundFrameTrailerCrcBytes))
    {
        return InboundFrameValidationResult::BadCrc;
    }

    return InboundFrameValidationResult::Ok;
}

} // namespace detail

// ---------- Chunk map ----------
// Shared between WsPool::nextChunk (now in ws_pool.cpp) and the Debug-only
// unit-test wrapper. The table itself stays TU-local; consumers reach it
// through the always-on `chunkSizeAtPosition` free function declared with
// external linkage so ws_pool.cpp links against the single live instance.
namespace
{

struct ChunkMap
{
    static constexpr int SEGSIZE = 131072;
    std::map<m_off_t, int> chunkmap;

    int chunksize(const m_off_t pos) const
    {
        const auto it = chunkmap.find(pos);
        return (it == chunkmap.end()) ? 8 * SEGSIZE : it->second;
    }

    ChunkMap()
    {
        m_off_t p{0};
        int dp{0};
        while (dp < 8 * SEGSIZE)
        {
            dp += SEGSIZE;
            chunkmap[p] = dp;
            p += dp;
        }
    }
};

ChunkMap g_chunkMap;

} // namespace

// Free function wrapper. Always-on (released from a former Debug-only gate)
// so ws_pool.cpp's WsPool::nextChunk can call it in Release builds too.
// Forward-declared in ws_pool.cpp; unit tests still extern-declare it.
int chunkSizeAtPosition(m_off_t pos)
{
    return g_chunkMap.chunksize(pos);
}

// ========== UploadEngine::Impl refresh-latch out-of-class definitions ==========
// Kept in this TU rather than the engine header so that
// WsPoolMgr::applyRefreshBackoff / applyRefreshedUrls can stay forward-declared
// at the header level. Both ws_pool_mgr.cpp definitions are visible here via
// the wsupload_engine.h → mega/transfer/ws/ws_pool_mgr.h chain.

bool UploadEngine::Impl::clearRefreshLatchForInstance(std::uint64_t id)
{
    std::lock_guard<std::mutex> g(uploadMutex);
    if (mInstanceId != id)
        return false;
    poolMgr.mRefreshing.store(false, std::memory_order_release);
    return true;
}

bool UploadEngine::Impl::applyRefreshResultForInstance(
    std::uint64_t id,
    Error e,
    std::vector<std::pair<std::string, m_off_t>>&& urls)
{
    std::lock_guard<std::mutex> g(uploadMutex);
    if (mInstanceId != id)
        return false;
    if (e == API_OK)
    {
        poolMgr.mRefreshFailCount = 0;
        poolMgr.mNextRefreshAttempt = 0;
        poolMgr.applyRefreshedUrls(std::move(urls));
    }
    else
    {
        poolMgr.applyRefreshBackoff(e);
    }
    poolMgr.mRefreshing.store(false, std::memory_order_release);
    return true;
}

// ========== UploadEngine (public facade) ==========
UploadEngine::UploadEngine(MegaClient& client):
    pImpl(new Impl(client))
{}

UploadEngine::~UploadEngine() = default;

std::uint64_t UploadEngine::instanceId() const noexcept
{
    return pImpl->instanceId();
}

bool UploadEngine::clearRefreshLatchForInstance(std::uint64_t id)
{
    return pImpl->clearRefreshLatchForInstance(id);
}

bool UploadEngine::applyRefreshResultForInstance(
    std::uint64_t id,
    Error e,
    std::vector<std::pair<std::string, m_off_t>>&& urls)
{
    return pImpl->applyRefreshResultForInstance(id, e, std::move(urls));
}

void UploadEngine::start()
{
    pImpl->start();
}

void UploadEngine::stop()
{
    pImpl->stop();
}

bool UploadEngine::isStopping() const
{
    return pImpl->stopping();
}

bool UploadEngine::workersQuiesced() const
{
    // S13 round-3 (Cluster I+J / SDK-6298 FU1 §4.1): cover the manager thread too.
    // Pool-worker-only quiesce let ~Impl's uploadThread.join() hang unbounded and
    // unlogged when the manager was wedged — the "Logout failed after 600 seconds"
    // round-3 CI job-killers. True now means EVERY engine thread body has exited,
    // so the destructor joins are guaranteed prompt.
    return pImpl->mLiveWorkerThreads.load(std::memory_order_acquire) == 0 &&
           !pImpl->mManagerThreadLive.load(std::memory_order_acquire);
}

int UploadEngine::liveWorkerCount() const
{
    return pImpl->mLiveWorkerThreads.load(std::memory_order_acquire);
}

bool UploadEngine::managerThreadLive() const
{
    return pImpl->mManagerThreadLive.load(std::memory_order_acquire);
}

void UploadEngine::enqueue(Transfer& t)
{
    pImpl->enqueue(t);
}

void UploadEngine::reposition(Transfer& t, Transfer* before)
{
    pImpl->reposition(t, before);
}

void UploadEngine::pause(Transfer& t)
{
    pImpl->pause(t);
}

void UploadEngine::unpause(Transfer& t)
{
    pImpl->unpause(t);
}

void UploadEngine::remove(Transfer& t)
{
    pImpl->remove(t);
}

void UploadEngine::setRetryUntil(Transfer& t, const dstime when)
{
    pImpl->setRetryUntil(t, when);
}

void UploadEngine::markFailed(Transfer& t, const dstime retryUntil)
{
    pImpl->markFailed(t, retryUntil);
}

bool UploadEngine::isUploading(Transfer& t) const
{
    return pImpl->isUploading(t);
}

bool UploadEngine::drainConfirmedChunkMacs(Transfer& t, std::vector<chunkmac_map>& out)
{
    return pImpl->drainConfirmedChunkMacs(t, out);
}

bool UploadEngine::getSessionUrl(Transfer& t, std::string& outUrl) const
{
    return pImpl->getSessionUrl(t, outUrl);
}

void UploadEngine::setCallbacks(UploadEngine::Callbacks cb)
{
    pImpl->mCb = std::move(cb);
}

bool UploadEngine::getTransferStats(const Transfer& t, UploadEngine::WsTransferStats& stats) const
{
    return pImpl->getTransferStats(t, stats);
}

// Forwarders always-compile (the hook-ABI struct is unconditional); see wsupload.h note above.
bool UploadEngine::getPoolStateForTesting(const std::string& url,
                                          UploadEngine::PoolStateForTesting& out) const
{
    return pImpl->getPoolStateForTesting(url, out);
}

bool UploadEngine::getWsUploadStatsForTesting(WsUploadStatsForTesting& out) const
{
    return pImpl->getWsUploadStatsForTesting(out);
}

UploadEngine::BenchThrottleSnapshot UploadEngine::getAndResetBenchThrottleStats()
{
    return pImpl->getAndResetBenchThrottleStats();
}

bool UploadEngine::isTrackedForTesting(const Transfer& t) const
{
    return pImpl->isTrackedForTesting(t);
}

std::uintptr_t UploadEngine::getFilePoolIdForTesting(Transfer& t) const
{
    return pImpl->getFilePoolIdForTesting(t);
}

void UploadEngine::kick()
{
    pImpl->kick();
}

void UploadEngine::notifyWorkers()
{
    pImpl->notifyWorkers();
}

void UploadEngine::notifyNetworkDisconnect()
{
    pImpl->notifyNetworkDisconnect();
}

void UploadEngine::setMaxConnections(const unsigned char maxConnections)
{
    pImpl->setMaxConnections(maxConnections);
}

void UploadEngine::setMaxUploadSpeed(const m_off_t bytesPerSecond)
{
    pImpl->setMaxUploadSpeed(bytesPerSecond);
}

// Simple feature gate for now (could later inspect client caps/settings)
bool wsEnabled(const MegaClient&)
{
    return true;
}

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD
