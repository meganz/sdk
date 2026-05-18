/**
 * @file src/transfer/ws/wsupload_engine.h
 * @brief Full definition of `UploadEngine::Impl`, the queue + manager-thread
 *        + pool-coordinator pImpl body backing the public `UploadEngine`
 *        facade declared in `mega/wsupload.h`.
 *
 *        Promoted out of `src/transfer/ws/wsupload.cpp` in fu7-16 Goal 2.Step0
 *        so that sibling WS-internal translation units (future ws_pool.cpp,
 *        ws_conn.cpp, ws_pool_mgr.cpp) can dereference `mImpl->X` members
 *        without ODR-violating duplicate definitions. See
 *        `Goal2_cross_followup_refactor_sweep/domain_coupling_map.md` (§5
 *        SS-1) for the coupling rationale.
 *
 *        NOT part of the public SDK API: lives under src/ rather than
 *        include/ so consumers cannot include it. Sibling translation units
 *        in src/transfer/ws/ include it as `#include "wsupload_engine.h"`
 *        (relative to the source file).
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
#include "wsupload_internal.h"

// Full definition of `class WsUploadFile` (split out of wsupload.cpp in fu7-16
// Goal 2.Step2). Needed because the inline member bodies of
// `UploadEngine::Impl` below dereference WsUploadFile members
// (`f->fileno()`, `f->setPool(...)`, `f->paused()`, etc.).
#include "ws_upload_file.h"

#include "mega/megaclient.h" // MegaClient, TransferDbCommitter, wsPostToClientThread
#include "mega/testhooks.h" // DEBUG_TEST_HOOK_WS_SESSION_URL_TRANSITION
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
class UploadEngine::Impl
{
public:
    explicit Impl(MegaClient& c):
        client(c)
    {
        mInstanceId = ++sInstanceCounter;
        //curl_global_init(CURL_GLOBAL_ALL);
        WSUPLOAD_TRACE << "[UploadEngine::Impl] constructed";
    }

    ~Impl()
    {
        WSUPLOAD_TRACE << "[UploadEngine::Impl::~Impl] BEGIN";
        stop();

        // Manager thread may be waiting up to WSUPLOAD_CURL_MULTI_POLL_MS in curl_multi_poll; then it exits.
        if (uploadThread.joinable())
            uploadThread.join();

        // Join pool worker threads before member destruction (uploadMutex must remain valid).
        // Note: automatic member destruction runs in reverse declaration order, so poolMgr
        // is destroyed AFTER uploadMutex. By the time we reach this point, every WsConn
        // owned by a pool worker has been destroyed by the worker's local unique_ptr as the
        // worker thread exited, which is why we only join here and do not touch mConns.
        for (auto& p: poolMgr.mPools)
        {
            if (!p)
                continue;
            for (auto& th: p->mActiveThreads)
            {
                if (th)
                    th->join();
            }
            for (auto& th: p->mExitingThreads)
            {
                if (th)
                    th->join();
            }
        }

        WSUPLOAD_TRACE << "[UploadEngine::Impl::~Impl] END";
    }

    void stop()
    {
        mStopping.store(true, std::memory_order_release);

        // Stop everything under the same mutex the workers use.
        std::lock_guard<std::mutex> g(uploadMutex);
        paused = true;
        uploadThreadRunning = false; // lets run() break out

        // Ask pool worker threads to exit promptly.
        for (auto& p: poolMgr.mPools)
        {
            p->mNumberOfConnections = 0; // avoids new workers
            for (auto& th: p->mActiveThreads)
                th->terminate = true;
            for (auto& th: p->mExitingThreads)
                th->terminate = true;
        }

        notifyWorkersLocked();
    }

    bool stopping() const
    {
        return mStopping.load(std::memory_order_acquire);
    }

    std::uint64_t instanceId() const noexcept
    {
        return mInstanceId;
    }

    // Refresh-latch orchestration helpers used by WsPoolMgr::refreshPools() callbacks.
    // Implementations live near other UploadEngine::Impl out-of-class definitions.
    bool clearRefreshLatchForInstance(std::uint64_t id);
    bool applyRefreshResultForInstance(std::uint64_t id,
                                       Error e,
                                       std::vector<std::pair<std::string, m_off_t>>&& urls);

    // Queue mirrors TransferList ordering and priority.
    void enqueue(Transfer& t)
    {
        WSUPLOAD_TRACE << "[UploadEngine::Impl::enqueue] t=" << t.localfilename
                  << " files.size=" << files.size() << " [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        if (!nextFileNo)
            nextFileNo = 1;

        // ws_fileno may come from transfer cache restore (Transfer::unserialize).
        // Reuse it when possible so resumed WS state keeps the same file identity.
        std::uint32_t fileno = t.ws_fileno;
        if (!fileno || fileByNo.find(fileno) != fileByNo.end())
        {
            fileno = nextFileNo++;
            t.ws_fileno = fileno;
        }
        else if (fileno >= nextFileNo)
        {
            // Keep nextFileNo above any restored file numbers to avoid collisions.
            nextFileNo = fileno + 1;
        }

        // ws_session_url may also come from transfer cache restore.
        // If present, create/keep a dedicated pinned pool for that exact endpoint.
        if (!t.ws_session_url.empty())
        {
            poolMgr.ensurePinnedPool(t.ws_session_url);
        }

        auto uf = std::make_unique<WsUploadFile>(client, t, fileno);
        auto raw = uf.get();

        fileList.push_back(raw);
        files.emplace(&t, std::move(uf));
        inQueue.emplace(raw);
        fileByNo.emplace(raw->fileno(), raw);

        if (fileList.size() == 1)
            nextIt = fileList.begin();
        bumpQueueVersion();
    }

    void reposition(Transfer& t, Transfer* before)
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(&t);
        if (it == files.end())
            return;

        auto* f = it->second.get();
        if (!f || !inQueue.count(f))
            return;

        // remove from current spot
        eraseFromFileListLocked(f);

        // insert before (if valid), else push back
        if (before)
        {
            auto itb = files.find(before);
            if (itb != files.end() && inQueue.count(itb->second.get()))
            {
                for (auto lit = fileList.begin(); lit != fileList.end(); ++lit)
                {
                    if (*lit == itb->second.get())
                    {
                        fileList.insert(lit, f);
                        bumpQueueVersion();
                        return;
                    }
                }
            }
        }

        fileList.push_back(f);
        bumpQueueVersion();
    }

    void pause(Transfer& t)
    {
        withFile(t,
                 [](WsUploadFile& f)
                 {
                     f.setPaused(true);
                 });
    }

    void unpause(Transfer& t)
    {
        withFile(t,
                 [](WsUploadFile& f)
                 {
                     f.setPaused(false);
                 });
    }

    void remove(Transfer& t)
    {
        std::unique_ptr<WsUploadFile> removed;
        {
            std::lock_guard<std::mutex> g(uploadMutex);
            auto it = files.find(&t);
            if (it == files.end())
                return;

            removed = std::move(it->second);
            auto* f = removed.get();
            if (!f)
                return;

            WSUPLOAD_TRACE << "Removing transfer from queue: " << f->fileno();

            poolMgr.mActiveFiles.erase(f);
            for (auto& poolPtr: poolMgr.mPools)
            {
                if (!poolPtr)
                    continue;
                WsPool& pool = *poolPtr;
                if (pool.mUploadingFile == f)
                {
                    pool.clearUploadingFileLocked();
                    pool.mUFTQversion = queueVersion.load(std::memory_order_relaxed);
                }
                pool.purgeFileLocked(f->fileno());
            }
            f->unsetPool();

            eraseFromFileListLocked(f);

            if (nextIt == fileList.end())
                nextIt = fileList.begin();

            inQueue.erase(f);
            fileByNo.erase(f->fileno());
            files.erase(it);
            bumpQueueVersion();
        }

        if (removed)
            removed->waitForNoIO();
    }

    void setRetryUntil(Transfer& t, const dstime when)
    {
        withFile(t,
                 [when](WsUploadFile& f)
                 {
                     f.setRetryUntil(when);
                 });
    }

    void markFailed(Transfer& t, const dstime retryUntil)
    {
        withFile(t,
                 [retryUntil](WsUploadFile& f)
                 {
                     f.markFailedForRetry(retryUntil);
                 });
    }

    bool isUploading(Transfer& t) const
    {
        WSUPLOAD_TRACE << "[UploadEngine::Impl::isUploading] t=" << t.localfilename
                  << " [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        if (const auto it = files.find(&t); it != files.end())
            return it->second->isUploading();
        WSUPLOAD_TRACE << "[UploadEngine::Impl::isUploading] t=" << t.localfilename
                  << " not found [this = " << this << "]";
        return false;
    }

    bool isUploading(const Transfer& t) const
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(const_cast<Transfer*>(&t));
        if (it == files.end() || !it->second)
            return false;
        const WsUploadFile* f = it->second.get();
        return f->inPool();
    }

    bool getTransferStats(const Transfer& t, UploadEngine::WsTransferStats& stats) const
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(const_cast<Transfer*>(&t));
        if (it == files.end() || !it->second)
            return false;
        return it->second->getTransferStats(stats);
    }

    bool drainConfirmedChunkMacs(Transfer& t, std::vector<chunkmac_map>& out)
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        const auto it = files.find(&t);
        if (it == files.end() || !it->second)
            return false;
        it->second->drainConfirmedChunkMacs(out);
        return true;
    }

    bool getSessionUrl(Transfer& t, std::string& outUrl) const
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        const auto it = files.find(&t);
        if (it == files.end() || !it->second)
        {
            return false;
        }
        return it->second->getCurrentSessionUrl(outUrl);
    }

    // fu7-15 G2.a-3: signatures always-compile so WsUploadDebugHelpers.h can include
    // this header in hooks-OFF builds. Bodies internally gate via
    // MEGASDK_DEBUG_TEST_HOOKS_ENABLED — in hooks-OFF they return a default-constructed
    // out with `found=false`, since the NDEBUG-only internal counters they would
    // otherwise read are not compiled in.
    bool getPoolStateForTesting(const std::string& url,
                                UploadEngine::PoolStateForTesting& out) const
    {
        out = {};
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        std::lock_guard<std::mutex> g(uploadMutex);
        auto populateState = [&out](const WsPool& pool)
        {
            out.found = true;
            out.pinned = pool.mPinned;
            out.retiring = pool.mRetiring;
            out.numPoolFiles = pool.mNumPoolFiles;
            out.hasUploadingFile = pool.mUploadingFile != nullptr;
            out.numChunksInFlight = pool.mNumChunksInFlight;
            out.queuedResends = static_cast<unsigned>(pool.mToResend.size());
            out.activeThreads = static_cast<unsigned>(pool.mActiveThreads.size());
            out.exitingThreads = static_cast<unsigned>(pool.mExitingThreads.size());
            out.openConnections = pool.countOpenConnectionsLocked();
            out.connectionsWithInFlight = pool.countConnectionsWithInFlightLocked();
            out.maxConnectionsWithInFlightSeen =
                std::max(pool.mMaxConnectionsWithInFlightSeen, out.connectionsWithInFlight);
            out.pausedByServerUntilDs = pool.mPausedByServerUntil;
        };

        for (const auto& poolPtr: poolMgr.mPools)
        {
            if (!poolPtr || !poolPtr->mPinned || poolPtr->mUrl != url)
            {
                continue;
            }

            populateState(*poolPtr);
            out.hasReference = poolMgr.pinnedPoolHasReference(*poolPtr, *this);
            return true;
        }

        for (const auto& poolPtr: poolMgr.mPools)
        {
            if (!poolPtr || poolPtr->mUrl != url)
            {
                continue;
            }

            populateState(*poolPtr);
            out.hasReference = poolMgr.pinnedPoolHasReference(*poolPtr, *this);
            return true;
        }

        return false;
#else
        (void)url;
        return false;
#endif
    }

    bool getWsUploadStatsForTesting(UploadEngine::WsUploadStatsForTesting& out) const
    {
        out = {};
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        std::lock_guard<std::mutex> g(uploadMutex);
        out.found = true;
        for (const auto& poolPtr: poolMgr.mPools)
        {
            if (poolPtr)
            {
                poolPtr->addWsUploadStatsForTesting(out);
            }
        }

        return true;
#else
        return false;
#endif
    }

    bool isTrackedForTesting(const Transfer& t) const
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        return files.find(const_cast<Transfer*>(&t)) != files.end();
    }

    std::uintptr_t getFilePoolIdForTesting(Transfer& t) const
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        if (const auto it = files.find(&t);
            it != files.end() && it->second && it->second->hasPool())
        {
            return reinterpret_cast<std::uintptr_t>(it->second->mPool);
        }
        return 0;
    }

    void invalidatePinnedSessionUrl(const std::string& url)
    {
        if (url.empty() || stopping())
        {
            return;
        }

        struct Victim
        {
            direction_t type;
            UploadHandle uploadhandle;
            Transfer* transfer;
        };

        std::vector<Victim> victims;
        {
            std::unique_lock<std::mutex> lk(uploadMutex);
            for (auto it = files.begin(); it != files.end(); ++it)
            {
                auto* tp = it->first;
                const auto& uf = it->second;
                if (uf && uf->sessionUrlHint() == url)
                {
                    victims.push_back(Victim{tp->type, tp->uploadhandle, tp});
                }
            }

            if (victims.empty())
            {
                return;
            }

            // Update in-memory WS engine state immediately without waiting for client-thread work.
            for (const auto& v: victims)
            {
                const auto it = files.find(v.transfer);
                if (it != files.end() && it->second)
                {
                    WsUploadFile* uf = it->second.get();
                    uf->clearSessionUrlHintAndRestart();

                    // If the transfer was already bound to the soon-to-be-retired pinned pool,
                    // detach it so it can be picked by fresh (non-pinned) pools.
                    WsPool* const pool = uf->mPool;
                    if (pool && pool->mPinned && pool->mUrl == url)
                    {
                        if (pool->mUploadingFile == uf)
                        {
                            pool->clearUploadingFileLocked();
                            pool->mUFTQversion = queueVersion.load(std::memory_order_relaxed);
                        }

                        pool->purgeFileLocked(uf->fileno());
                        uf->unsetPool();
                    }
                }
            }

            // Retire pinned pools for this URL so we don't keep retrying an expired endpoint.
            for (auto& pool: poolMgr.mPools)
            {
                if (pool && pool->mPinned && pool->mUrl == url)
                {
                    poolMgr.markPoolRetiring(*pool);
                }
            }

            bumpQueueVersion();
        }

        client.wsPostToClientThread(
            [victims, url](MegaClient& client, TransferDbCommitter& committer)
            {
                for (const auto& v: victims)
                {
                    if (!client.wsIsTransferAlive(v.type, v.transfer))
                    {
                        continue;
                    }

                    auto* tp = v.transfer;
                    if (!tp->uploadhandle.eq(v.uploadhandle))
                    {
                        continue;
                    }

                    // This invalidate task runs asynchronously; skip if transfer already switched
                    // to a new session URL, so stale work cannot clear newer session state.
                    if (!tp->ws_session_url.empty() && tp->ws_session_url != url)
                    {
                        continue;
                    }

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                    const std::string prevUrlForHook = tp->ws_session_url;
#endif
                    tp->ws_session_url.clear();
                    tp->chunkmacs.clear();
                    tp->pos = 0;
                    tp->setProgresscompleted(0);
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                    DEBUG_TEST_HOOK_WS_SESSION_URL_TRANSITION(tp->tag,
                                                              prevUrlForHook,
                                                              std::string{},
                                                              "invalidatePinned");
#endif

                    client.transfercacheadd(tp, &committer);
                    if (client.app)
                    {
                        client.app->transfer_update(tp);
                    }
                }
            });
    }

    void start()
    {
        std::thread stoppedUploadThread;
        std::vector<std::unique_ptr<WsPoolThread>> stoppedPoolThreads;

        {
            std::lock_guard<std::mutex> g(uploadMutex);
            if (uploadThread.joinable())
            {
                if (uploadThreadRunning.load(std::memory_order_acquire))
                    return;

                stoppedUploadThread = std::move(uploadThread);

                for (auto& pool: poolMgr.mPools)
                {
                    if (!pool)
                        continue;

                    for (auto& th: pool->mActiveThreads)
                    {
                        if (th)
                            th->terminate = true;
                        stoppedPoolThreads.push_back(std::move(th));
                    }
                    pool->mActiveThreads.clear();

                    for (auto& th: pool->mExitingThreads)
                    {
                        if (th)
                            th->terminate = true;
                        stoppedPoolThreads.push_back(std::move(th));
                    }
                    pool->mExitingThreads.clear();
                }
            }
        }

        if (stoppedUploadThread.joinable())
            stoppedUploadThread.join();

        // Join worker threads outside uploadMutex to avoid lock-order inversion with WsConn
        // teardown (~WsConn() acquires uploadMutex).
        stoppedPoolThreads.clear();

        std::lock_guard<std::mutex> g(uploadMutex);
        if (uploadThread.joinable())
            return;

        mStopping.store(false, std::memory_order_release);
        paused = false;
        // Defensive: ensure the refresh gate is not stuck from a prior engine lifecycle
        // where the posted clearRefreshing lambda was dropped before execution.
        poolMgr.mRefreshing.store(false, std::memory_order_release);
        poolMgr.mImpl = this;
        for (auto& pool: poolMgr.mPools)
        {
            if (!pool || pool->mRetiring)
                continue;

            pool->setPoolNumConn(mPoolConnectionLimit);
        }
        poolMgr.refreshPools();

        uploadThread = std::thread(
            [this]
            {
                this->run();
            });
    }

    void kick()
    {
        WSUPLOAD_TRACE << "[UploadEngine::Impl::kick] BEGIN [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        // if (poolMgr.mPools.empty())
        //     poolMgr.refreshPools();
        for (auto& p: poolMgr.mPools)
        {
            WSUPLOAD_TRACE << "[UploadEngine::Impl::kick] pool(" << (void*)p.get()
                      << ") checkThreads() [this = " << this << "]";
            p->checkThreads();
        }
        WSUPLOAD_TRACE << "[UploadEngine::Impl::kick] END [numPools=" << poolMgr.mPools.size()
                  << "] [this = " << this << "]";
    }

    // Requires uploadMutex to be held by caller.
    void notifyWorkersLocked()
    {
        ++workerWakeEpoch;
        workerWakeCv.notify_all();
    }

    void notifyWorkers()
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        notifyWorkersLocked();
    }

    void notifyNetworkDisconnect()
    {
        disconnectEpoch.fetch_add(1, std::memory_order_release);
        notifyWorkers();
    }

    unsigned char poolConnectionLimit() const
    {
        return mPoolConnectionLimit;
    }

    void setMaxConnections(const unsigned char maxConnections)
    {
        const auto newLimit = std::max<unsigned char>(1, maxConnections);

        std::lock_guard<std::mutex> g(uploadMutex);
        mPoolConnectionLimit = newLimit;

        for (auto& pool: poolMgr.mPools)
        {
            if (!pool || pool->mRetiring)
            {
                continue;
            }

            pool->setPoolNumConn(newLimit);
        }
    }

    // Must be called with uploadMutex held.
    bool consumeUploadBudget(const m_off_t bytes, dstime* retryAfterDs = nullptr)
    {
        if (bytes <= 0 || mMaxUploadSpeed <= 0)
        {
            return true;
        }

        const dstime now = SteadyTime::ds();
        if (!mUploadBudgetLastDs)
        {
            mUploadBudgetLastDs = now;
        }

        const dstime elapsedDs = now > mUploadBudgetLastDs ? now - mUploadBudgetLastDs : 0;
        if (elapsedDs > 0)
        {
            const m_off_t maxValue = std::numeric_limits<m_off_t>::max();
            const m_off_t elapsed = static_cast<m_off_t>(elapsedDs);
            const m_off_t scale = static_cast<m_off_t>(SpeedController::DS_PER_SECOND);

            // Avoid overflow if uploading was pending for an unusually long time.
            const m_off_t budgetIncrement =
                (mMaxUploadSpeed > (maxValue / elapsed)) ?
                    maxValue :
                    (mMaxUploadSpeed * elapsed) / scale;
            if (budgetIncrement > (maxValue - mUploadBudget))
            {
                mUploadBudget = maxValue;
            }
            else
            {
                mUploadBudget += budgetIncrement;
            }

            // Keep burst behavior bounded after long idle/pending periods.
            const m_off_t burstWindowSeconds =
                static_cast<m_off_t>(SpeedController::SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS);
            const m_off_t maxBurstBudget =
                (mMaxUploadSpeed > (maxValue / burstWindowSeconds)) ?
                    maxValue :
                    (mMaxUploadSpeed * burstWindowSeconds);
            const m_off_t budgetCap = std::max(maxBurstBudget, bytes);
            if (mUploadBudget > budgetCap)
            {
                mUploadBudget = budgetCap;
            }
            mUploadBudgetLastDs = now;
        }

        if (mUploadBudget < bytes)
        {
            if (retryAfterDs)
            {
                const m_off_t deficit = bytes - mUploadBudget;
                const m_off_t numerator =
                    deficit * static_cast<m_off_t>(SpeedController::DS_PER_SECOND) + mMaxUploadSpeed - 1;
                const dstime suggestedDs = static_cast<dstime>(numerator / mMaxUploadSpeed);
                *retryAfterDs = std::clamp<dstime>(suggestedDs, 1, 10);
            }
            return false;
        }

        mUploadBudget -= bytes;
        return true;
    }

    void setMaxUploadSpeed(const m_off_t bytesPerSecond)
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        mMaxUploadSpeed = std::max<m_off_t>(bytesPerSecond, 0);
        mUploadBudget = 0;
        mUploadBudgetLastDs = SteadyTime::ds();
    }

    // Called by pools to pick next file that matches [min,max)
    WsUploadFile* nextEligible(const m_off_t min,
                               const m_off_t max,
                               const std::string* requiredSessionUrl,
                               const WsPool* requestingPool)
    {
        WSUPLOAD_TRACE << "[UploadEngine::Impl::nextEligible] BEGIN [fileList.size=" << fileList.size()
                  << "] [this = " << this << "]";

        bool consecutive = true;
        if (!fileList.empty())
        {
            auto it = nextIt;
            for (std::size_t scanned = 0; scanned < fileList.size(); ++scanned)
            {
                if (it == fileList.end())
                {
                    it = fileList.begin();
                }

                WsUploadFile* f = *it;
                if (!f)
                {
                    WSUPLOAD_TRACE << "[UploadEngine::Impl::nextEligible] !f -> continue [this = "
                              << this << "]";
                    ++it;
                    continue;
                }

                const bool poolEligible = !f->hasPool() || f->mPool == requestingPool;
                if (poolEligible && !f->paused() && f->continuingUpload(currentTime) &&
                    f->hasPendingBytesOrEofToSend())
                {
                    // Retiring pools only drain already-owned files, shall not pick any new unbound work.
                    if (requestingPool && requestingPool->mRetiring && !f->hasPool())
                    {
                        ++it;
                        continue;
                    }

                    const auto& hint = f->sessionUrlHint();
                    if (requiredSessionUrl)
                    {
                        if (hint.empty() || hint != *requiredSessionUrl)
                        {
                            ++it;
                            continue;
                        }
                    }
                    else if (!hint.empty())
                    {
                        // Reserved for the pool bound to this specific session URL.
                        ++it;
                        continue;
                    }

                    if (f->size() >= min && (!max || f->size() < max))
                    {
                        WSUPLOAD_TRACE << "[UploadEngine::Impl::nextEligible] f->size(=" << f->size()
                                  << ") >= min(=" << min << ") && (!max(=" << max
                                  << ") || f->size(=" << f->size() << ") < max(=" << max
                                  << ")) -> candidate selected [consecutive=" << consecutive
                                  << "] [this = " << this << "]";
                        if (consecutive)
                        {
                            advanceNextItFrom(it);
                        }
                        return f;
                    }

                    consecutive = false;
                }

                ++it;
            }
        }
        WSUPLOAD_TRACE << "[UploadEngine::Impl::nextEligible] END - return nullptr [this = " << this
                  << "]";
        return nullptr;
    }

#ifndef NDEBUG
    bool hasEligibleFileForPoolForTesting(const m_off_t min,
                                          const m_off_t max,
                                          const std::string* requiredSessionUrl,
                                          const WsPool* requestingPool) const
    {
        for (WsUploadFile* f: fileList)
        {
            if (!f)
            {
                continue;
            }

            const bool poolEligible = !f->hasPool() || f->mPool == requestingPool;
            if (!poolEligible || f->paused() || !f->continuingUpload(currentTime) ||
                !f->hasPendingBytesOrEofToSend())
            {
                continue;
            }

            const auto& hint = f->sessionUrlHint();
            if (requiredSessionUrl)
            {
                if (hint.empty() || hint != *requiredSessionUrl)
                {
                    continue;
                }
            }
            else if (!hint.empty())
            {
                continue;
            }

            if (f->size() >= min && (!max || f->size() < max))
            {
                return true;
            }
        }

        return false;
    }
#endif

    // Manager thread
    void run()
    {
        uploadThreadRunning = true;
        std::unique_lock<std::mutex> lk(uploadMutex);
        while (uploadThreadRunning)
        {
            currentTime = SteadyTime::ds();

            // Pump USC/aux cURL
            poolMgr.curlIO(lk);

            // per-file throughput (server-ack basis)
            for (WsUploadFile* f: poolMgr.mActiveFiles)
                f->maybeReportThroughput(currentTime);
            poolMgr.mActiveFiles.clear();

            if (!paused)
                poolMgr.checkPools(*this);

            cleanupExitedPoolThreads(lk);
        }
        uploadThreadRunning = false;
    }

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
    void cleanupExitedPoolThreads(std::unique_lock<std::mutex>& lk)
    {
        std::vector<std::unique_ptr<WsPoolThread>> finished;

        for (auto& pool: poolMgr.mPools)
        {
            if (!pool)
                continue;

            for (std::size_t i = pool->mExitingThreads.size(); i-- > 0;)
            {
                auto& th = pool->mExitingThreads[i];
                if (!th || !th->terminated)
                    continue;

                finished.push_back(std::move(th));
                pool->mExitingThreads.erase(pool->mExitingThreads.begin() +
                                            static_cast<std::ptrdiff_t>(i));
            }
        }

        if (!finished.empty())
        {
            // Destroying WsPoolThread invokes join(); do it without uploadMutex to avoid
            // lock-order inversion with worker-thread teardown (~WsConn() acquires uploadMutex).
            ScopedUnlock unlock(lk);
            finished.clear();
        }
    }

    void bumpQueueVersion()
    {
        const std::uint32_t newVersion =
            queueVersion.fetch_add(1, std::memory_order_relaxed) + 1;
        for (auto& pool: poolMgr.mPools)
        {
            if (pool && pool->mUploadingFile && inQueue.count(pool->mUploadingFile))
                pool->mUFTQversion = newVersion;
        }
    }

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
    void eraseFromFileListLocked(WsUploadFile* f)
    {
        for (auto lit = fileList.begin(); lit != fileList.end(); ++lit)
        {
            if (*lit == f)
            {
                if (nextIt == lit)
                    ++nextIt;
                fileList.erase(lit);
                return;
            }
        }
    }
};

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD

#endif // MEGA_TRANSFER_WS_WSUPLOAD_ENGINE_H
