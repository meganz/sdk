/**
 * @file include/mega/transfer/ws/wsupload_engine.h
 * @brief Declaration of `UploadEngine::Impl`, the queue + manager-thread
 *        + pool-coordinator pImpl body backing the public `UploadEngine`
 *        facade declared in `mega/wsupload.h`.
 *
 *        Promoted out of `src/transfer/ws/wsupload.cpp` in fu7-16 Goal 2.Step0
 *        so that sibling WS-internal translation units (ws_pool.cpp,
 *        ws_conn.cpp, ws_pool_mgr.cpp) can dereference `mImpl->X` members
 *        without ODR-violating duplicate definitions.
 *
 *        fu7-17 Goal 2.a split this header into declarations-only + a sibling
 *        `src/transfer/ws/wsupload_engine.cpp` for the non-trivial method
 *        bodies. Trivial accessors and the `withFile<F>` private template
 *        helper remain inline in this header.
 *
 *        SDK-internal architecture header (fu7-17 Goal 1.a relocated it from
 *        `src/transfer/ws/` to `include/mega/transfer/ws/` alongside
 *        `ws_encryption.h` and `ws_pool_mgr.h`). Sibling translation units in
 *        `src/transfer/ws/` and `src/megaclient_wsupload.cpp` /
 *        `src/commands_ws.cpp` include it as
 *        `#include "mega/transfer/ws/wsupload_engine.h"`.
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

#ifndef MEGA_TRANSFER_WS_WSUPLOAD_ENGINE_H
#define MEGA_TRANSFER_WS_WSUPLOAD_ENGINE_H 1

#ifdef MEGA_USE_WSUPLOAD

// File-internal types (WsPool, WsConn, WsPoolThread, SteadyTime, ScopedUnlock,
// WSUPLOAD_TRACE, FailReason) plus, via its own includes, mega/wsupload.h
// (UploadEngine, Callbacks) and mega/transfer/ws/ws_pool_mgr.h (WsPoolMgr).
#include "mega/transfer/ws/wsupload_internal.h"

// Full definition of `class WsUploadFile` (split out of wsupload.cpp in fu7-16
// Goal 2.Step2). Needed because the inline `withFile<F>` template body below
// dereferences WsUploadFile members.
#include "mega/transfer/ws/ws_upload_file.h"

// `mega/megaapp.h` must precede `mega/megaclient.h` (which only forward-declares
// `struct MegaApp* app`) so that sibling TUs see `MegaApp` as a complete type —
// the `invalidatePinnedSessionUrl` body in wsupload_engine.cpp posts a lambda
// that calls `client.app->transfer_update(tp)`. Keeping these includes here in
// the header keeps every TU that includes `wsupload_engine.h` self-contained.
#include "mega/megaapp.h"
#include "mega/megaclient.h" // MegaClient, TransferDbCommitter, wsPostToClientThread
#include "mega/transfer.h" // Transfer, SpeedController

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mega
{
namespace ws
{

// ========== UploadEngine::Impl (queue + mgr + thread) ==========
//
// All non-trivial method bodies live in src/transfer/ws/wsupload_engine.cpp
// (fu7-17 Goal 2.a). This header keeps:
//   - The class declaration + member variables.
//   - Trivial one-line accessors (stopping/instanceId/poolConnectionLimit).
//   - The inline ctor.
//   - The private `withFile<F>` template (mandatory: template definition must
//     be visible to every TU that instantiates it).
//   - The private one-line noexcept iterator helpers `cycleNextIt` and
//     `advanceNextItFrom`.
class UploadEngine::Impl
{
public:
    explicit Impl(MegaClient& c):
        client(c)
    {
        mInstanceId = ++sInstanceCounter;
        WSUPLOAD_TRACE << "[UploadEngine::Impl] constructed";
    }

    ~Impl();

    void stop();

    bool stopping() const
    {
        return mStopping.load(std::memory_order_acquire);
    }

    std::uint64_t instanceId() const noexcept
    {
        return mInstanceId;
    }

    // Refresh-latch orchestration helpers used by WsPoolMgr::refreshPools() callbacks.
    // Out-of-class definitions live in wsupload.cpp (not wsupload_engine.cpp) because
    // they only need the WsPoolMgr declaration, which is included via
    // wsupload_internal.h's chain — and keeping them next to the public
    // UploadEngine facade definitions in wsupload.cpp simplifies the ownership story.
    bool clearRefreshLatchForInstance(std::uint64_t id);
    bool applyRefreshResultForInstance(std::uint64_t id,
                                       Error e,
                                       std::vector<std::pair<std::string, m_off_t>>&& urls);

    // Queue mirrors TransferList ordering and priority.
    void enqueue(Transfer& t);

    void reposition(Transfer& t, Transfer* before);

    void pause(Transfer& t);

    void unpause(Transfer& t);

    void remove(Transfer& t);

    void setRetryUntil(Transfer& t, const dstime when);

    void markFailed(Transfer& t, const dstime retryUntil);

    bool isUploading(Transfer& t) const;

    bool isUploading(const Transfer& t) const;

    bool getTransferStats(const Transfer& t, UploadEngine::WsTransferStats& stats) const;

    bool drainConfirmedChunkMacs(Transfer& t, std::vector<chunkmac_map>& out);

    bool getSessionUrl(Transfer& t, std::string& outUrl) const;

    // fu7-15 G2.a-3: signatures always-compile so WsUploadDebugHelpers.h can include
    // this header in hooks-OFF builds. Bodies internally gate via
    // MEGASDK_DEBUG_TEST_HOOKS_ENABLED — in hooks-OFF they return a default-constructed
    // out with `found=false`, since the NDEBUG-only internal counters they would
    // otherwise read are not compiled in.
    bool getPoolStateForTesting(const std::string& url,
                                UploadEngine::PoolStateForTesting& out) const;

    bool getWsUploadStatsForTesting(UploadEngine::WsUploadStatsForTesting& out) const;

    // fu7-19 G7: Release-safe — see UploadEngine::getAndResetBenchThrottleStats.
    UploadEngine::BenchThrottleSnapshot getAndResetBenchThrottleStats();

    bool isTrackedForTesting(const Transfer& t) const;

    std::uintptr_t getFilePoolIdForTesting(Transfer& t) const;

    void invalidatePinnedSessionUrl(const std::string& url);

    void start();

    void kick();

    // Requires uploadMutex to be held by caller.
    void notifyWorkersLocked()
    {
        ++workerWakeEpoch;
        workerWakeCv.notify_all();
    }

    void notifyWorkers();

    void notifyNetworkDisconnect();

    unsigned char poolConnectionLimit() const
    {
        return mPoolConnectionLimit;
    }

    void setMaxConnections(const unsigned char maxConnections);

    // Must be called with uploadMutex held.
    bool consumeUploadBudget(const m_off_t bytes, dstime* retryAfterDs = nullptr);

    void setMaxUploadSpeed(const m_off_t bytesPerSecond);

    // Called by pools to pick next file that matches [min,max)
    WsUploadFile* nextEligible(const m_off_t min,
                               const m_off_t max,
                               const std::string* requiredSessionUrl,
                               const WsPool* requestingPool);

#ifndef NDEBUG
    bool hasEligibleFileForPoolForTesting(const m_off_t min,
                                          const m_off_t max,
                                          const std::string* requiredSessionUrl,
                                          const WsPool* requestingPool) const;
#endif

    // Manager thread
    void run();

    // --- state ---
    MegaClient& client;

    using ListWsUploadFile = std::list<WsUploadFile*>;
    ListWsUploadFile fileList;
    std::unordered_map<Transfer*, std::unique_ptr<WsUploadFile>> files;
    std::unordered_set<WsUploadFile*> inQueue;
    std::unordered_map<std::uint32_t, WsUploadFile*> fileByNo;

    ListWsUploadFile::iterator nextIt = fileList.begin();
    // Atomic because poolWorkerThread reads queueVersion before acquiring
    // uploadMutex at wsupload.cpp:4315 (race with bumpQueueVersion writer).
    // Relaxed memory order is sufficient: the value is used as a change-counter
    // for "should I refreshPools?"; transitive ordering of work-state is
    // separately protected by uploadMutex.
    std::atomic<std::uint32_t> queueVersion{0};
    UploadEngine::Callbacks mCb{};

    WsPoolMgr poolMgr;

    mutable std::mutex uploadMutex;
    std::condition_variable workerWakeCv;
    std::thread uploadThread;
    std::atomic<bool> uploadThreadRunning{false};
    std::atomic<std::uint64_t> disconnectEpoch{0};
    std::uint64_t workerWakeEpoch{0};
    std::atomic<bool> mStopping{false};

    dstime currentTime{0};
    std::atomic<std::uint32_t> nextFileNo{1};
    unsigned char mPoolConnectionLimit{3};
    m_off_t mMaxUploadSpeed{0};
    m_off_t mUploadBudget{0};
    dstime mUploadBudgetLastDs{0};
    bool paused{false};
    inline static std::atomic<std::uint64_t> sInstanceCounter{0};
    std::uint64_t mInstanceId{0};

private:
    void cleanupExitedPoolThreads(std::unique_lock<std::mutex>& lk);

    void bumpQueueVersion();

    // Template helper: must stay in the header so every TU that instantiates
    // a different lambda type sees the definition. Used by pause/unpause/
    // setRetryUntil/markFailed bodies in wsupload_engine.cpp.
    template<class F>
    void withFile(Transfer& t, F&& fn)
    {
        WSUPLOAD_TRACE << "[UploadEngine::Impl::withFile] BEGIN [t=" << t.localfilename
                  << "] [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(&t);
        if (it == files.end() || !it->second)
            return;
        fn(*it->second);
        WSUPLOAD_TRACE << "[UploadEngine::Impl::withFile] END [this = " << this << "]";
    }

    void cycleNextIt() noexcept
    {
        if (!fileList.empty() && nextIt == fileList.end())
            nextIt = fileList.begin();
    }

    void advanceNextItFrom(ListWsUploadFile::iterator it) noexcept
    {
        nextIt = std::next(it);
        cycleNextIt();
    }

    // Linear-scan fileList for f and erase it, rebasing nextIt to the successor on hit.
    // Caller must hold uploadMutex. Does NOT cycle nextIt back to begin() on end() —
    // callers that need that semantics must follow up with cycleNextIt().
    void eraseFromFileListLocked(WsUploadFile* f);
};

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD

#endif // MEGA_TRANSFER_WS_WSUPLOAD_ENGINE_H
