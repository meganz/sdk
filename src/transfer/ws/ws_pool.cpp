/**
 * @file src/transfer/ws/ws_pool.cpp
 * @brief All `struct WsPool` member function bodies — split out of
 *        src/transfer/ws/wsupload.cpp.
 *
 *        Bodies hosted here (in declaration order):
 *
 *          - Debug stats: assignUploadingFileLocked, recordFirstByteSentLocked,
 *            recordUploadCompletedLocked, addWsUploadStatsForTesting,
 *            recordWsUploadStatsSampleLocked.
 *          - Picking: findPreflightReadyCandidate, getWsUploadFile, findFile,
 *            nextChunk.
 *          - Retry / apply: retryChunksOnTheWire / retryChunksOnTheWireLocked,
 *            retryChunkLocked, retryChunk, handshakeFailureCandidateLocked,
 *            purgeFileLocked, applyInFlightLocked, applyInFlight.
 *          - Chunk-prep + worker thread (HOT PATH): sendChunk, poolWorkerThread,
 *            checkThreads.
 *
 *        Couplings:
 *          - WsPool/UploadEngine::Impl (DEEP): poolWorkerThread (~20 derefs),
 *            sendChunk (~13), getWsUploadFile (~7), nextChunk (~6). Full Impl
 *            definition reached via mega/transfer/ws/wsupload_engine.h.
 *          - WsPool/WsConn / WsBuf / WsPoolThread (OK): all-public struct
 *            cluster, full type via mega/transfer/ws/wsupload_internal.h.
 *          - WsPool/WsUploadFile (OK): public accessor API, full type via
 *            mega/transfer/ws/ws_upload_file.h.
 *          - ws::detail / TU-local helpers (ChunkMap / g_chunkMap /
 *            chunkSizeAtPosition / steadyMs) stay in wsupload.cpp; this TU
 *            only consumes them via the inline accessors / via
 *            ws_upload_file.h re-exports for steadyMs (Debug builds).
 *
 *        WsPool's mega::ws::encryptChunk consumer needs ws_encryption.h. The
 *        WS chunk-encrypt API takes a stack-snapshotted SymmCipher key + ctrIv
 *        so this TU never touches the live cipher under uploadMutex.
 *
 *        The chunk-size table `g_chunkMap` and its inline accessor
 *        `g_chunkMap.chunksize(pos)` remain in wsupload.cpp because the
 *        ChunkMap struct is TU-local. The worker in this TU calls
 *        `chunkSizeAtPosition(pos)` declared in wsupload_internal.h; the
 *        single definition lives in wsupload.cpp so there is one table per
 *        process. The wrapper is a release-build always-on free function (it
 *        was previously Debug-only when only unit tests called it; sibling
 *        TUs now need it in Release too).
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

// File-internal types shared with wsupload.cpp (WsPool, WsConn, WsPoolThread,
// WsChunk, ChunkFingerprintMacUpdate, ScopedUnlock, SteadyTime, FailReason,
// WSUPLOAD_TRACE, kMiB). SDK-internal architecture header.
#include "mega/transfer/ws/wsupload_internal.h"

// Full WsUploadFile definition — every chunk-prep + worker-thread call site
// dereferences WsUploadFile members directly (paused/aborted/inPool/headPos/
// transferKey/ctrIv/readData/markEOF/unsetPool/uploadFailed/etc.).
#include "mega/transfer/ws/ws_upload_file.h"

// Full UploadEngine::Impl definition — sendChunk / poolWorkerThread /
// nextChunk / getWsUploadFile / handshakeFailureCandidateLocked etc. all
// dereference impl. / mImpl-> members (uploadMutex, queueVersion,
// disconnectEpoch, workerWakeCv/Epoch, stopping, mCb.{onFail,onStart,
// preflightStart,canStartAnotherFile}, fileByNo, files, fileList,
// consumeUploadBudget, currentTime, paused, poolConnectionLimit,
// invalidatePinnedSessionUrl, hasEligibleFileForPoolForTesting, nextEligible).
// Transitively includes mega/megaapp.h (UploadEngine::Impl's
// invalidatePinnedSessionUrl body posts a lambda that dereferences
// client.app), so this TU is self-contained.
#include "mega/transfer/ws/wsupload_engine.h"

#include "mega/logging.h"
#include "mega/testhooks.h" // DEBUG_TEST_HOOK_WS_CHUNK_SEND_OVERQUOTA / WSCONN_FORCE_CLOSE_NOW / WSPOOL_RECONNECT_ATTEMPT / WSUPLOAD_SUSTAINED_HANDSHAKE_FAILURE_WINDOW_DS
#include "mega/transfer/ws/ws_encryption.h" // mega::ws::encryptChunk
#include "mega/transfer/ws/ws_pool_mgr.h" // WsPoolMgr::refreshPools (poolWorkerThread)
#include "mega/wsupload.h" // UploadEngine::FailureDisposition / PreflightStartResult / WsUploadStatsForTesting

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>

namespace mega
{
namespace ws
{

// Forward decl of the TU-local chunk-size lookup defined in wsupload.cpp.
// The chunk-size table itself (`g_chunkMap`) is TU-local to wsupload.cpp and
// is not re-exported; `chunkSizeAtPosition` is the only consumer this TU needs.
// Already declared `extern int chunkSizeAtPosition(m_off_t)` under #ifndef NDEBUG
// in wsupload.cpp (Debug-only wrapper for unit tests). Step 4 promotes the
// declaration to be always-on so the WsPool bodies link against it in Release
// too. The definition in wsupload.cpp is now always-on (wrapping the same
// `g_chunkMap.chunksize(pos)` call) — the previous `#ifndef NDEBUG` gate is
// dropped (zero overhead in Release: still a single direct call).
int chunkSizeAtPosition(m_off_t pos);

// Steady-clock millisecond helper used by Debug-only book-keeping. Defined in
// wsupload.cpp under #ifndef NDEBUG. Forward-declared here so the Debug
// methods below link.
#ifndef NDEBUG
std::uint64_t steadyMs();
#endif

#ifndef NDEBUG
void WsPool::assignUploadingFileLocked(WsUploadFile* file) noexcept
{
    if (mUploadingFile == file)
    {
        return;
    }

    const auto now = steadyMs();
    if (mUploadingFile && mUploadingFileSinceMs)
    {
        mUploadingFileOccupiedMs += now - mUploadingFileSinceMs;
    }

    mUploadingFile = file;
    mUploadingFileSinceMs = file ? now : 0;
}

void WsPool::recordFirstByteSentLocked(WsUploadFile& file) noexcept
{
    if (!file.markFirstByteSentForTesting())
    {
        return;
    }

    const auto now = steadyMs();
    const auto fileno = file.fileno();
    mLastFirstByteSentMs = now;
    mLastFirstByteSentFileno = fileno;

    if (mLastCompletedFileMs && mLastCompletedFileno != fileno)
    {
        const auto deltaMs = now - mLastCompletedFileMs;
        ++mLastAckToNextFirstByteSamples;
        mLastAckToNextFirstByteTotalMs += deltaMs;
        mLastAckToNextFirstByteMaxMs = std::max(mLastAckToNextFirstByteMaxMs, deltaMs);
        mLastCompletedFileMs = 0;
        mLastCompletedFileno = 0;
    }
}

void WsPool::recordUploadCompletedLocked(const std::uint32_t fileno) noexcept
{
    const auto now = steadyMs();
    if (mLastFirstByteSentMs && mLastFirstByteSentFileno != fileno)
    {
        ++mLastAckToNextFirstByteSamples;
        mLastCompletedFileMs = 0;
        mLastCompletedFileno = 0;
        return;
    }

    mLastCompletedFileMs = now;
    mLastCompletedFileno = fileno;
}

void WsPool::addWsUploadStatsForTesting(UploadEngine::WsUploadStatsForTesting& out) const
{
    ++out.poolCount;
    out.uploadingFileOccupiedMs +=
        mUploadingFileOccupiedMs +
        ((mUploadingFile && mUploadingFileSinceMs) ? (steadyMs() - mUploadingFileSinceMs) : 0);
    out.lastAckToNextFirstByteSamples += mLastAckToNextFirstByteSamples;
    out.lastAckToNextFirstByteTotalMs += mLastAckToNextFirstByteTotalMs;
    out.lastAckToNextFirstByteMaxMs =
        std::max(out.lastAckToNextFirstByteMaxMs, mLastAckToNextFirstByteMaxMs);
    out.allChunksInFlightBlockedMs += mAllChunksInFlightBlockedMs;
    out.eligibleFileSampleCount += mEligibleFileSampleCount;
    out.blockedByInFlightSampleCount += mBlockedByInFlightSampleCount;
    out.idleEligibleConnectionMs += mIdleEligibleConnectionMs;
    out.idleEligibleConnectionSampleCount += mIdleEligibleConnectionSampleCount;

    // Send-side counters: per-pool aggregates.
    out.haveSpaceFalseIters += mHaveSpaceFalseIters;
    out.haveSpaceFalseWaitMs += mHaveSpaceFalseWaitMs;
    out.readyForDataFalseIters += mReadyForDataFalseIters;
    out.readyForDataFalseWaitMs += mReadyForDataFalseWaitMs;
    out.throttleSleepIters += mThrottleSleepIters;
    out.throttleSleepMs += mThrottleSleepMs;
    out.backlogEmptyIters += mBacklogEmptyIters;
    out.backlogEmptyMs += mBacklogEmptyMs;
    out.chunkPrepTotalMs += mChunkPrepTotalMs;
    if (mChunkPrepMaxMs > out.chunkPrepMaxMs)
        out.chunkPrepMaxMs = mChunkPrepMaxMs;
    out.chunkPrepN += mChunkPrepN;

    // Server throttle telemetry aggregates.
    out.throttleEventCount += mThrottleEventCount;
    out.throttleEventTotalDs += mThrottleEventTotalDs;
    out.throttleEventSumSqDs += mThrottleEventSumSqDs;
    if (out.throttleEventMinDs == 0 || (mThrottleEventMinDs && mThrottleEventMinDs < out.throttleEventMinDs))
        out.throttleEventMinDs = mThrottleEventMinDs;
    if (mThrottleEventMaxDs > out.throttleEventMaxDs) out.throttleEventMaxDs = mThrottleEventMaxDs;
    out.throttleBucket0to1s += mThrottleBucket0to1s;
    out.throttleBucket1to5s += mThrottleBucket1to5s;
    out.throttleBucket5to30s += mThrottleBucket5to30s;
    out.throttleBucket30sPlus += mThrottleBucket30sPlus;
    for (int i = 0; i < 16; ++i) out.throttleEventCodeCounts[i] += mThrottleEventCodeCounts[i];
    if (mSimultaneousThrottledConnsMax > out.simultaneousThrottledConnsMax)
        out.simultaneousThrottledConnsMax = mSimultaneousThrottledConnsMax;
    out.simultaneousThrottledConnsSamples += mSimultaneousThrottledConnsSamples;
    out.simultaneousThrottledConnsSum += mSimultaneousThrottledConnsSum;

    // Per-conn counters: aggregate across all live conns in this pool.
    for (const WsConn* c: mConns)
    {
        if (!c)
            continue;
        out.curlAgainSendCount += c->mCurlAgainSendCount;
        out.curlAgainRecvCount += c->mCurlAgainRecvCount;
        if (static_cast<std::uint64_t>(c->mBufferedAmountHighWater) > out.bufferedAmountHighWater)
            out.bufferedAmountHighWater =
                static_cast<std::uint64_t>(c->mBufferedAmountHighWater);
        if (static_cast<std::uint64_t>(c->mChunksInFlightHighWater) > out.chunksInFlightHighWater)
            out.chunksInFlightHighWater =
                static_cast<std::uint64_t>(c->mChunksInFlightHighWater);
        // Per-conn throttle-recovery aggregates.
        out.throttleRecoveryAckSamples += c->mThrottleRecoveryAckSamples;
        out.throttleRecoveryAckTotalMs += c->mThrottleRecoveryAckTotalMs;
        if (c->mThrottleRecoveryAckMaxMs > out.throttleRecoveryAckMaxMs)
            out.throttleRecoveryAckMaxMs = c->mThrottleRecoveryAckMaxMs;
    }
}

void WsPool::recordWsUploadStatsSampleLocked(const UploadEngine::Impl& impl)
{
    const auto now = steadyMs();
    if (!mLastCounterSampleMs)
    {
        mLastCounterSampleMs = now;
        return;
    }

    const auto elapsedMs = now - mLastCounterSampleMs;
    mLastCounterSampleMs = now;
    if (!elapsedMs)
    {
        return;
    }

    const bool hasEligibleFile = impl.hasEligibleFileForPool(mMinFileSize,
                                                                       mMaxFileSize,
                                                                       mPinned ? &mUrl : nullptr,
                                                                       this);
    if (!hasEligibleFile)
    {
        return;
    }

    ++mEligibleFileSampleCount;

    const unsigned openConnections = countOpenConnectionsLocked();
    const unsigned connectionsWithInFlight = countConnectionsWithInFlightLocked();
    const bool currentFileDrained =
        mUploadingFile && !mUploadingFile->hasPendingBytesOrEofToSend();

    if (currentFileDrained && mNumChunksInFlight > 0 && openConnections > 0 &&
        connectionsWithInFlight >= openConnections)
    {
        mAllChunksInFlightBlockedMs += elapsedMs;
        ++mBlockedByInFlightSampleCount;
    }

    if (openConnections > connectionsWithInFlight)
    {
        const auto idleConnections =
            static_cast<std::uint64_t>(openConnections - connectionsWithInFlight);
        mIdleEligibleConnectionMs += elapsedMs * idleConnections;
        mIdleEligibleConnectionSampleCount += idleConnections;
    }
}
#endif // NDEBUG (Debug-only stats block)

// Release-safe bench-framework hook: atomically swaps each per-pool throttle
// counter to zero and accumulates the previous value into `out`. `exchange`
// with memory_order_relaxed is sufficient because the bench framework only
// reads these at iter boundaries (no ordering relationship with surrounding
// data).
void WsPool::addAndResetBenchThrottleStatsTo(UploadEngine::BenchThrottleSnapshot& out)
{
    out.event6Count += mBenchThrottleEvent6Count.exchange(0, std::memory_order_relaxed);
    out.event6TotalMs += mBenchThrottleEvent6TotalMs.exchange(0, std::memory_order_relaxed);
    out.pauseCount += mBenchThrottlePauseCount.exchange(0, std::memory_order_relaxed);
    out.pauseTotalMs += mBenchThrottlePauseTotalMs.exchange(0, std::memory_order_relaxed);
}

// ========== WsPool ==========

WsUploadFile* WsPool::findPreflightReadyCandidate(UploadEngine::Impl& impl)
{
    const std::size_t scanBudget = impl.fileList.size();
    for (std::size_t scanned = 0; scanned < scanBudget; ++scanned)
    {
        auto* candidate = impl.nextEligible(mMinFileSize, mMaxFileSize, mPinned ? &mUrl : nullptr, this);
        if (!candidate)
        {
            break;
        }

        if (impl.mCb.preflightStart)
        {
            const auto preflightResult = impl.mCb.preflightStart(candidate->transfer());
            if (preflightResult != UploadEngine::PreflightStartResult::Ready)
            {
                mPreflightPending = true;
                continue;
            }
        }

        // A ready candidate was selected in this round, clear pending hints.
        mPreflightPending = false;
        return candidate;
    }

    return nullptr;
}

bool WsPool::getWsUploadFile(const dstime now, UploadEngine::Impl& impl)
{
    // Mark true when we observe preflight pending (queued/running) so worker threads
    // use short CV waits instead of coarse decisecond sleeps.
    mPreflightPending = false;

    const std::uint32_t implQueueVersion = impl.queueVersion.load(std::memory_order_relaxed);
    if (mUploadingFile && !mUploadingFile->paused() && mUploadingFile->continuingUpload(now) &&
        mUploadingFile->hasPendingBytesOrEofToSend() && mUFTQversion == implQueueVersion)
    {
        WSUPLOAD_TRACE << "[WsPool::getWsUploadFile] mUploadingFile->paused()=false && "
                     "mUploadingFile->continuingUpload(now) && mUFTQversion(="
                  << mUFTQversion << ") == impl.queueVersion(=" << implQueueVersion
                  << ") -> return true [this = " << this << "]";
        return true;
    }

    // Back‑pressure gate: defer starting a new file while FA pipeline is saturated
    if (impl.mCb.canStartAnotherFile && !impl.mCb.canStartAnotherFile())
    {
        WSUPLOAD_TRACE << "[WsPool::getWsUploadFile] impl.mCb.canStartAnotherFile=true && "
                     "!impl.mCb.canStartAnotherFile() -> return false [this = "
                  << this << "]";
        return false;
    }

    WsUploadFile* f = findPreflightReadyCandidate(impl);
    if (!f)
    {
        WSUPLOAD_TRACE << "[WsPool::getWsUploadFile] !f -> return false [this = " << this << "]";
        return false;
    }

    WSUPLOAD_TRACE << "[WsPool::getWsUploadFile] candidate selected [f=" << (void*)f
              << "] [this = " << this << "]";

    // snapshot write-once crypto material now that preflightStart has
    // guaranteed f->transfer().transferkey and ctriv are populated.
    f->snapshotCryptoMaterial(f->transfer());
    // Defer start-marking until preflight succeeds so failed preflight does not leave
    // the file stuck in an active attempt state.
    f->setUploadStart(impl.currentTime);

    if (mUploadingFile != f)
    {
        WSUPLOAD_TRACE << "[WsPool::getWsUploadFile] mUploadingFile(=" << (void*)mUploadingFile
                  << ") != f(=" << (void*)f << ") -> picking file fileno=" << f->fileno()
                  << " for [" << mMinFileSize << "," << mMaxFileSize
                  << ") pool and set mUFTQversion(=" << implQueueVersion
                  << ") = impl.queueVersion(=" << implQueueVersion << ") [this = " << this
                  << "]";
#ifndef NDEBUG
        assignUploadingFileLocked(f);
#else
        mUploadingFile = f;
#endif
        mUFTQversion = implQueueVersion;
        mUploadingFile->setPool(*this);
        if (impl.mCb.onStart)
            impl.mCb.onStart(mUploadingFile->transfer());
    }
    else
    {
        WSUPLOAD_TRACE << "[WsPool::getWsUploadFile] mUploadingFile(=" << (void*)mUploadingFile
                  << ") == f(=" << (void*)f << ") -> return true [this = " << this << "]";
    }
    return true;
}

WsUploadFile* WsPool::findFile(const std::uint32_t fileno, UploadEngine::Impl& impl)
{
    if (mUploadingFile && mUploadingFile->fileno() == fileno)
        return (mUploadingFile->mPool == this) ? mUploadingFile : nullptr;

    auto it = impl.fileByNo.find(fileno);
    if (it == impl.fileByNo.end())
        return nullptr;
    return (it->second && it->second->mPool == this) ? it->second : nullptr;
}

bool WsPool::nextChunk(WsChunk& chunk, UploadEngine::Impl& impl, dstime* retryAfterDs)
{
    WSUPLOAD_TRACE << "[WsPool::nextChunk] BEGIN [this = " << this << "]";
    // queued retry first
    while (!mToResend.empty())
    {
        chunk = mToResend.front();
        // Discard stale resend entries whose target file is gone or whose byte range no
        // longer fits the current attempt (e.g. resetAttemptState rewound mHeadPos = 0
        // but the entry was queued under a previous attempt). Re-sending out-of-bounds
        // bytes would break the server-side sliding window; bytes the server already has
        // would be re-acked via opcode 2, but never-fitting ranges must be dropped here.
        {
            WsUploadFile* const ufStale = findFile(chunk.fileno, impl);
            if (!ufStale || (chunk.len && chunk.pos + chunk.len > ufStale->size()))
            {
                WSUPLOAD_TRACE << "[WsPool::nextChunk] discard stale resend entry [pos=" << chunk.pos
                          << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno << "]";
                mToResend.erase(mToResend.begin());
                continue;
            }
        }
        if (!impl.consumeUploadBudget(static_cast<m_off_t>(chunk.len), retryAfterDs))
        {
            // Pool is alive but throttled. Keep it fresh so SERVERTIMEOUT does not
            // force an unnecessary refresh.
            mLastActive = impl.currentTime;
            return false;
        }
        mToResend.erase(mToResend.begin());
        WSUPLOAD_TRACE << "WsUpload: resending chunk pos=" << chunk.pos << " len=" << chunk.len
                  << " fileno=" << chunk.fileno;
        if (findFile(chunk.fileno, impl))
            return true; // file still valid?
    }

    if (impl.paused)
    {
        WSUPLOAD_TRACE << "[WsPool::nextChunk] impl.paused=true -> return false [this = " << this << "]";
        return false;
    }

    while (getWsUploadFile(impl.currentTime, impl))
    {
        if (mUploadingFile->headPos() < mUploadingFile->size() || !mUploadingFile->eofSet())
        {
            WSUPLOAD_TRACE << "[WsPool::nextChunk] mUploadingFile->headPos(="
                      << mUploadingFile->headPos()
                      << ") < mUploadingFile->size(=" << mUploadingFile->size()
                      << ") || !mUploadingFile->eofSet(=" << mUploadingFile->eofSet()
                      << ") -> process chunk"
                      << " [this = " << this << "]";
            chunk.fileno = mUploadingFile->fileno();

            if (mUploadingFile->headPos() == mUploadingFile->size())
            {
                // empty EOF chunk
                mUploadingFile->markEOF();
                chunk.pos = mUploadingFile->headPos();
                chunk.len = 0;
            }
            else
            {
                chunk.pos = mUploadingFile->headPos();

                const int advance = chunkSizeAtPosition(chunk.pos);
                m_off_t newHead = chunk.pos + advance;

                if (newHead > mUploadingFile->size())
                {
                    newHead = mUploadingFile->size();
                    mUploadingFile->markEOF();
                }

                chunk.len = static_cast<int>(newHead - chunk.pos);

                if (!impl.consumeUploadBudget(static_cast<m_off_t>(chunk.len), retryAfterDs))
                {
                    // Pool is alive but throttled. Keep it fresh so SERVERTIMEOUT does not
                    // force an unnecessary refresh.
                    mLastActive = impl.currentTime;
                    return false;
                }
                mUploadingFile->advanceHead(chunk.len);
            }

            mLastActive = impl.currentTime;
            return true;
        }
        WSUPLOAD_TRACE << "[WsPool::nextChunk] mUploadingFile->headPos() < mUploadingFile->size() || "
                     "!mUploadingFile->eofSet() -> mUploadingFile = nullptr [this = "
                  << this << "]";
        clearUploadingFileLocked(); // done with this file
    }
    WSUPLOAD_TRACE << "[WsPool::nextChunk] END - !getWsUploadFile -> return false [this = " << this
              << "]";
    return false;
}

void WsPool::retryChunksOnTheWire(WsConn* ws)
{
    std::lock_guard<std::mutex> g(mImpl->uploadMutex);
    retryChunksOnTheWireLocked(ws);
}

void WsPool::retryChunksOnTheWireLocked(WsConn* ws)
{
    WSUPLOAD_TRACE << "WsUpload: WS to " << mUrl << " lost; rescheduling " << ws->mChunksInFlight.size()
              << " in-flight chunks";
    for (auto& p: ws->mChunksInFlight)
    {
        mToResend.push_back(p.first);
    }
    mNumChunksInFlight -= static_cast<int>(ws->mChunksInFlight.size());
    ws->mChunksInFlight.clear();
}

void WsPool::retryChunkLocked(const WsChunk& chunk)
{
    // Per-chunk retry cap for CrcFailed (opcode 3). Transfer-level retry is already
    // covered by Transfer::failed; this cap is specifically for the narrow case where
    // the server keeps rejecting the same chunk via opcode 3, which would otherwise
    // loop forever consuming bandwidth without escalating to the app.
    static constexpr unsigned kMaxChunkRetries = 10;
    WsChunk retry = chunk;
    ++retry.retryCount;
    if (retry.retryCount > kMaxChunkRetries)
    {
        LOG_warn << "[WsPool::retryChunkLocked] per-chunk retry cap hit, failing upload "
                    "[fileno="
                 << chunk.fileno << "] [pos=" << chunk.pos << "] [retryCount=" << retry.retryCount
                 << "] [this = " << this << "]";
        WsUploadFile* const uf = findFile(chunk.fileno, *mImpl);
        purgeFileLocked(chunk.fileno);
        if (uf)
        {
            uf->uploadFailed(FailReason::Protocol);
            if (mImpl->mCb.onFail)
                mImpl->mCb.onFail(uf->transfer(),
                                  API_EINTERNAL,
                                  chunk.pos,
                                  UploadEngine::FailureDisposition::Retryable);
        }
        return;
    }
    mToResend.push_back(retry);
}

void WsPool::retryChunk(const WsChunk& chunk)
{
    std::lock_guard<std::mutex> g(mImpl->uploadMutex);
    retryChunkLocked(chunk);
}

WsUploadFile* WsPool::handshakeFailureCandidateLocked(const dstime now) const
{
    auto eligibleInPool = [this, now](WsUploadFile* f)
    {
        return f && f->mPool == this && !f->paused() && f->continuingUpload(now);
    };

    if (eligibleInPool(mUploadingFile))
    {
        return mUploadingFile;
    }

    // mUploadingFile can temporarily be null while the same pool still owns a resumable upload.
    for (const auto& kv: mImpl->fileByNo)
    {
        if (eligibleInPool(kv.second))
        {
            return kv.second;
        }
    }

    // Fallback: a transfer can be briefly unbound from this pool while still pinned to this
    // session URL hint during reconnect/refresh transitions.
    auto eligibleByHint = [this, now](WsUploadFile* f)
    {
        return f && !f->hasPool() && f->sessionUrlHint() == mUrl && !f->paused() &&
               f->continuingUpload(now);
    };
    for (const auto& kv: mImpl->fileByNo)
    {
        if (eligibleByHint(kv.second))
        {
            return kv.second;
        }
    }

    return nullptr;
}

void WsPool::purgeFileLocked(const std::uint32_t fileno)
{
    // Drop any queued resends for this file.
    for (auto it = mToResend.begin(); it != mToResend.end();)
    {
        if (it->fileno == fileno)
        {
            it = mToResend.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Drop any in-flight records for this file.
    for (auto& conn: mConns)
    {
        if (!conn)
            continue;
        for (auto it = conn->mChunksInFlight.begin(); it != conn->mChunksInFlight.end();)
        {
            if (it->first.fileno == fileno)
            {
                it = conn->mChunksInFlight.erase(it);
                if (mNumChunksInFlight > 0)
                    --mNumChunksInFlight;
            }
            else
            {
                ++it;
            }
        }
    }
}

void WsPool::applyInFlightLocked(const std::uint32_t fileno)
{
    WsUploadFile* uf = findFile(fileno, *mImpl);
    for (auto& conn: mConns)
    {
        for (auto it = conn->mChunksInFlight.begin(); it != conn->mChunksInFlight.end();)
        {
            if (it->first.fileno == fileno)
            {
                if (uf)
                {
                    it->second.apply(it->first.pos, *uf);
                }
                it = conn->mChunksInFlight.erase(it);
                if (mNumChunksInFlight > 0)
                    --mNumChunksInFlight;
            }
            else
            {
                ++it;
            }
        }
    }
}

void WsPool::applyInFlight(const std::uint32_t fileno)
{
    std::lock_guard<std::mutex> g(mImpl->uploadMutex);
    applyInFlightLocked(fileno);
}

bool WsPool::sendChunk(WsConn* ws, UploadEngine::Impl& impl, dstime* retryAfterDs)
{
    if (ws->readyState.load(std::memory_order_relaxed) != WsConn::ReadyState::OPEN || !ws->haveSpace())
    {
        WSUPLOAD_TRACE << "[WsPool::sendChunk] ws->readyState="
                  << static_cast<int>(ws->readyState.load(std::memory_order_relaxed))
                  << " (ReadyState::OPEN=" << static_cast<int>(WsConn::ReadyState::OPEN)
                  << ") ws->haveSpace=" << ws->haveSpace() << " -> return false [this = " << this
                  << "]";
        return false;
    }

    WsChunk chunk;
    // Zero the out param so the worker loop's initializer (0) controls the
    // fallback path.
    if (retryAfterDs)
    {
        *retryAfterDs = 0;
    }

    if (!nextChunk(chunk, impl, retryAfterDs))
    {
        WSUPLOAD_TRACE << "[WsPool::sendChunk] !nextChunk -> return false [this = " << this << "]";
        return false;
    }

    static thread_local std::unique_ptr<char[]> tlsBuf;
    if (!tlsBuf)
    {
        WSUPLOAD_TRACE << "[WsPool::sendChunk] !tlsBuf -> tlsBuf.reset(new char[kMiB]) [this = " << this
                  << "]";
        tlsBuf.reset(new char[kMiB]);
    }

    WsUploadFile* uf = findFile(chunk.fileno, impl);
    const auto expectedGeneration = uf ? uf->workGeneration() : 0;
    bool interruptedByStateChange = false;
    const bool okRead = uf && !uf->aborted() &&
                        (chunk.len == 0 || uf->readData(tlsBuf.get(),
                                                        chunk.pos,
                                                        chunk.len,
                                                        impl.uploadMutex,
                                                        expectedGeneration,
                                                        &interruptedByStateChange));
    if (okRead)
    {
        uf = findFile(chunk.fileno, impl);
        if (!uf)
        {
            WSUPLOAD_TRACE << "[WsPool::sendChunk] drop chunk after read (file no longer in map) [pos="
                      << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno
                      << "] [this = " << this << "]";
            return false;
        }
        if (!uf->inPool() || uf->aborted())
        {
            WSUPLOAD_TRACE << "[WsPool::sendChunk] drop chunk after read (no longer in pool or aborted) "
                         "[pos="
                      << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno
                      << "] [this = " << this << "]";
            return false;
        }
        if (uf->paused())
        {
            WSUPLOAD_TRACE << "[WsPool::sendChunk] requeue chunk after read (paused) [pos=" << chunk.pos
                      << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno
                      << "] [this = " << this << "]";
            mToResend.push_back(chunk);
            return false;
        }
        assert(uf->isUploading() && "invariant: upload must be active after successful read "
                                    "with inPool, !aborted, !paused checks passing; the "
                                    "mWorkGeneration match inside readData guarantees the "
                                    "attempt did not reset underneath us");

        ChunkFingerprintMacUpdate update(chunk.pos);
        if (chunk.len)
        {
            const unsigned pad =
                static_cast<unsigned>((-(int)chunk.len) & (SymmCipher::BLOCKSIZE - 1));
            if (pad)
                std::memset(tlsBuf.get() + chunk.len, 0, pad);

            // SDK-5360 worker-parallel encrypt/MAC: snapshot crypto material to
            // local stack and release impl.uploadMutex around encrypt+MAC so
            // multiple worker threads can encrypt concurrently using their own
            // thread_local SymmCipher (see mega::ws::encryptChunk). Re-validate
            // inPool/aborted/paused after re-locking, mirroring the existing
            // post-readData re-checks above.
            std::array<byte, SymmCipher::KEYLENGTH> localKey = uf->transferKey();
            const int64_t localCtrIv = uf->ctrIv();
            const m_off_t localChunkPos = chunk.pos;
            const int localChunkLen = chunk.len;

            impl.uploadMutex.unlock();
            chunkmac_map macs = mega::ws::encryptChunk(localKey,
                                                      localCtrIv,
                                                      localChunkPos,
                                                      localChunkLen,
                                                      reinterpret_cast<byte*>(tlsBuf.get()));
            impl.uploadMutex.lock();

            // Re-validate uf state. uf may have been pool-detached or aborted while
            // we were unlocked. Re-look up by fileno to also catch the (rare) case
            // where uf was replaced.
            uf = findFile(chunk.fileno, impl);
            if (!uf || !uf->inPool() || uf->aborted())
            {
                LOG_debug << "[WsPool::sendChunk] drop chunk after lock-released encrypt "
                             "(uf invalidated) [pos="
                          << localChunkPos << "] [fileno=" << chunk.fileno
                          << "] [this = " << this << "]";
                return false;
            }
            if (uf->paused())
            {
                LOG_debug << "[WsPool::sendChunk] requeue chunk after lock-released encrypt "
                             "(paused) [pos="
                          << localChunkPos << "] [fileno=" << chunk.fileno
                          << "] [this = " << this << "]";
                mToResend.push_back(chunk);
                return false;
            }
            update = ChunkFingerprintMacUpdate(chunk.pos, std::move(macs));
        }
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        // Test seam: deterministically inject OVERQUOTA on this chunk-send by
        // short-circuiting through the same purgeFileLocked + mCb.onFail pattern
        // that the local-I/O-error path below uses. Drives the existing
        // Transfer::failed(API_EOVERQUOTA, ..., 0) plumbing — does NOT introduce
        // a new propagation path. Hook fires with impl.uploadMutex held, after uf
        // has been re-validated post-encrypt, so chunk.fileno / uf->transfer().tag
        // are stable for the duration of the call.
        {
            bool injectOverquota = false;
            DEBUG_TEST_HOOK_WS_CHUNK_SEND_OVERQUOTA(uf->transfer().tag, injectOverquota);
            if (injectOverquota)
            {
                LOG_warn << "[WsPool::sendChunk] test hook requested OVERQUOTA injection "
                            "[pos="
                         << chunk.pos << "] [fileno=" << chunk.fileno
                         << "] [tag=" << uf->transfer().tag << "] [this = " << this << "]";
                purgeFileLocked(chunk.fileno);
                uf->uploadFailed(FailReason::ServerError);
                if (impl.mCb.onFail)
                {
                    impl.mCb.onFail(uf->transfer(),
                                    API_EOVERQUOTA,
                                    chunk.pos,
                                    UploadEngine::FailureDisposition::Retryable);
                }
                return false;
            }
        }
#endif
        WSUPLOAD_TRACE << "[WsPool::sendChunk] uf->readData(tlsBuf.get(), chunk.pos, chunk.len, "
                     "impl.uploadMutex) -> emplace mChunksInFlight, sendChunkData && return true "
                     "[mNumChunksInFlight="
                  << mNumChunksInFlight << "] [this = " << this << "]";
        ws->mChunksInFlight.emplace_back(chunk, std::move(update));
#ifndef NDEBUG
        updateMaxConnectionsWithInFlightSeenLocked();
        if (chunk.len > 0)
        {
            recordFirstByteSentLocked(*uf);
        }
        // Per-conn chunks-in-flight high-water.
        if (ws->mChunksInFlight.size() > ws->mChunksInFlightHighWater)
            ws->mChunksInFlightHighWater = static_cast<unsigned>(ws->mChunksInFlight.size());
#endif
        uf->onRequestSent();
        ws->sendChunkData(chunk.fileno, chunk.pos, tlsBuf.get(), chunk.len);

        ++mNumChunksInFlight;
        return true;
    }
    uf = findFile(chunk.fileno, impl);
    if (uf && chunk.len && uf->inPool() && !uf->aborted() && interruptedByStateChange)
    {
        // Always requeue: if the attempt was paused, the chunk sends after unpause;
        // if the attempt was reset via resetAttemptState (setUploadStart / markFailed /
        // uploadFailed / uploadCompleted), mHeadPos is 0 and nextChunk will skip this
        // stale entry on drain. Never drop silently — there is no server opcode that
        // recovers a never-sent byte range.
        WSUPLOAD_TRACE << "[WsPool::sendChunk] requeue chunk after interrupted read/open [pos="
                  << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno << "]";
        mToResend.push_back(chunk);
        return false;
    }

    // Local I/O error (or other non-paused failure while reading/opening).
    // Abort this upload attempt and let MegaClient run the standard Transfer::failed retry/backoff
    // logic.
    if (uf && !uf->aborted())
    {
        LOG_warn << "[WsPool::sendChunk] read/open failed, aborting upload attempt [pos="
                 << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno << "]";

        purgeFileLocked(chunk.fileno);

        if (uf->hasFailed())
        {
            // readData() already called markFailed() (which includes resetAttemptState);
            // only the pool detach that uploadFailed() normally does is still needed.
            uf->unsetPool();
        }
        else
        {
            uf->uploadFailed(FailReason::ReadFailed);
        }

        if (impl.mCb.onFail)
        {
            impl.mCb.onFail(uf->transfer(), API_EREAD, chunk.pos, uf->readFailureDisposition());
        }
        return false;
    }

    WSUPLOAD_TRACE << "[WsPool::sendChunk] read/open failed but file is no longer active in this pool"
              << " [pos=" << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno
              << "]";
    return false;
}

void WsPool::poolWorkerThread(WsPoolThread* th)
{
    int retryCount{0};
    dstime firstConnectFailureDs{0};
    std::uint32_t lastQueueVersion = mImpl->queueVersion.load(std::memory_order_relaxed);
    std::uint64_t seenDisconnectEpoch = mImpl->disconnectEpoch.load(std::memory_order_acquire);
    std::unique_ptr<WsConn> ws;  // fu7-21 Lever F: lazy-allocated at loop top

    WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] BEGIN [lastQueueVersion=" << lastQueueVersion
              << "] [this = " << this << "]";

    std::unique_lock<std::mutex> lk(mImpl->uploadMutex);
    while (!th->terminate)
    {
        // fu7-21 Lever F (FU-RSS): non-pinned size-class pools defer the WsConn
        // allocation + connect until they have eligible work, so idle
        // non-matching size-class pools hold zero WsBuf (~2 MiB/conn). The
        // matching pool sees its file on the first iteration and connects exactly
        // as before. Pinned pools keep eager-connect (their failover timing is
        // exercised by InvalidPinned* and must not change).
        if (!ws)
        {
            bool eligible = mPinned;
            if (!eligible)
            {
                const bool poolHasWork = mNumPoolFiles || mUploadingFile ||
                                         mNumChunksInFlight || !mToResend.empty();
                eligible = poolHasWork ||
                           mImpl->hasEligibleFileForPool(mMinFileSize, mMaxFileSize,
                                                         nullptr, this);
            }
            if (!eligible)
            {
                const auto wakeEpochBeforeIdle = mImpl->workerWakeEpoch;
                mImpl->workerWakeCv.wait_for(
                    lk,
                    std::chrono::milliseconds(200),
                    [&]
                    {
                        return th->terminate || mImpl->stopping() ||
                               mImpl->workerWakeEpoch != wakeEpochBeforeIdle;
                    });
                continue;
            }
            ScopedUnlock unlock(lk);
            ws = std::make_unique<WsConn>(this);
        }

        const std::uint64_t disconnectEpoch =
            mImpl->disconnectEpoch.load(std::memory_order_acquire);
        if (disconnectEpoch != seenDisconnectEpoch)
        {
            seenDisconnectEpoch = disconnectEpoch;
            if (ws->readyState.load(std::memory_order_relaxed) != WsConn::ReadyState::CLOSED)
            {
                // Reuse normal close path so in-flight chunks are re-queued safely.
                ScopedUnlock unlock(lk);
                ws->closeWS();
            }
        }

        if (ws->readyState.load(std::memory_order_relaxed) == WsConn::ReadyState::CLOSED)
        {
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
            DEBUG_TEST_HOOK_WSPOOL_RECONNECT_ATTEMPT(this,
                                                    static_cast<unsigned>(retryCount),
                                                    firstConnectFailureDs);
#endif
            // Do not hold the engine mutex while blocking on connect.
            bool ok = false;
            {
                ScopedUnlock unlock(lk);
                ok = ws->connectWS();
            }

            if (!ok)
            {
                const dstime nowDs = SteadyTime::ds();
                if (!firstConnectFailureDs)
                {
                    firstConnectFailureDs = nowDs;
                }

                if (!retryCount++)
                {
                    WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] retryCount=" << retryCount
                              << " -> continue [this = " << this << "]";
                    continue;
                }

                // Pinned session invalidation should be conservative: brief network glitches can
                // cause a few connect failures, but do not necessarily mean the pinned endpoint
                // is invalid. Require both a minimum retry count and sustained failure window
                // (60s).
                const auto failedForDs = SteadyTime::difference(nowDs, firstConnectFailureDs);
                if (mPinned && !mRetiring && retryCount >= 3 && failedForDs >= secondsToDs(60))
                {
                    LOG_warn << "[WsPool::poolWorkerThread] pinned session URL failed to connect "
                                "after retries: "
                             << mUrl << " [retryCount=" << retryCount
                             << "] [failedForDs=" << failedForDs << "] [this = " << this << "]";
                    ScopedUnlock unlock(lk);
                    mImpl->invalidatePinnedSessionUrl(mUrl);
                    continue;
                }

                // Repeated handshake failures for an active upload must eventually transition
                // through Transfer::failed/backoff instead of looping forever in reconnect.
                dstime sustainedHandshakeFailureWindowDs = UPLOADTIMEOUT;
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                DEBUG_TEST_HOOK_WSUPLOAD_SUSTAINED_HANDSHAKE_FAILURE_WINDOW_DS(
                    sustainedHandshakeFailureWindowDs);
#endif
                if (retryCount >= 3 &&
                    failedForDs >= sustainedHandshakeFailureWindowDs)
                {
                    auto* uploadToFail = handshakeFailureCandidateLocked(nowDs);
                    if (uploadToFail)
                    {
                        LOG_warn << "[WsPool::poolWorkerThread] sustained WS handshake failures for "
                                    "active upload -> fail current attempt"
                                 << " [url=" << mUrl << "] [retryCount=" << retryCount
                                 << "] [failedForDs=" << failedForDs
                                 << "] [retiring=" << mRetiring
                                 << "] [fileno=" << uploadToFail->fileno() << "] [this = " << this
                                 << "]";

                        // Reset the active-file fast path so the eventual retry must re-enter
                        // nextEligible()/setUploadStart()/onStart() instead of silently reusing the
                        // stale active file after backoff expires.
                        purgeFileLocked(uploadToFail->fileno());
                        uploadToFail->markFailedForRetry(0);
                        uploadToFail->unsetPool();
                        if (mUploadingFile == uploadToFail)
                        {
                            clearUploadingFileLocked();
                        }
                        mUFTQversion = mImpl->queueVersion.load(std::memory_order_relaxed);

                        if (mImpl->mCb.onFail)
                        {
                            mImpl->mCb.onFail(uploadToFail->transfer(),
                                              API_EAGAIN,
                                              0,
                                              UploadEngine::FailureDisposition::Retryable);
                        }

                        // Reset connect-failure window for the next attempt.
                        retryCount = 0;
                        firstConnectFailureDs = 0;
                        continue;
                    }
                }

                const std::uint32_t curQueueVersion =
                    mImpl->queueVersion.load(std::memory_order_relaxed);
                if (!mRetiring && lastQueueVersion != curQueueVersion)
                {
                    WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] !mRetiring && lastQueueVersion("
                              << lastQueueVersion << ") != mImpl->queueVersion("
                              << curQueueVersion << ") -> refreshPools [this = " << this << "]";
                    mImpl->poolMgr.refreshPools();
                }

                WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] !ok -> continue [retryCount=" << retryCount
                          << "] [this = " << this << "]";

                {
                    ScopedUnlock unlock(lk);
                    SteadyTime::sleep_ds(CONNRETRYINTERVAL);
                }
                continue;
            }
            retryCount = 0;
            firstConnectFailureDs = 0;
        }

        lastQueueVersion = mImpl->queueVersion.load(std::memory_order_relaxed);

        // recv server frames
        {
            ScopedUnlock unlock(lk);
            ws->curlRecv();
        }

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        // B9 regression hook: lets a test deterministically force the WsConn into CLOSED
        // after a server ACK (e.g. a Throttle event) so it can observe reconnect pacing.
        {
            bool forceClose = false;
            DEBUG_TEST_HOOK_WSCONN_FORCE_CLOSE_NOW(ws.get(), this, mUrl, forceClose);
            if (forceClose)
            {
                WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] force-close via test hook [this = "
                          << this << "]";
                ScopedUnlock unlock(lk);
                ws->closeWS();
                continue;
            }
        }
#endif

        // server throttle?
        if (throttledByServer())
        {
            WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] throttledByServer=true -> continue [this = "
                      << this << "]";
#ifndef NDEBUG
            // Sample simultaneous throttled conns at throttle wait point.
            {
                unsigned simul = 0;
                for (const auto* c: mConns) if (c && c->mPauseStartedAtMs) ++simul;
                ++mSimultaneousThrottledConnsSamples;
                mSimultaneousThrottledConnsSum += simul;
                if (simul > mSimultaneousThrottledConnsMax) mSimultaneousThrottledConnsMax = simul;
            }
#endif
            {
                ScopedUnlock unlock(lk);
                SteadyTime::sleep_ds(1);
            }
#ifndef NDEBUG
            ++mThrottleSleepIters;
            mThrottleSleepMs += 100;
#endif
            continue;
        }

        // flush send buffers
        {
            ScopedUnlock unlock(lk);
            ws->curlSend();
        }

        if (!ws->haveSpace())
        {
            WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] haveSpace=false -> continue [this = " << this
                      << "]";
            {
                ScopedUnlock unlock(lk);
                SteadyTime::sleep_ds(HAVE_SPACE_RETRY_DS);
            }
#ifndef NDEBUG
            ++mHaveSpaceFalseIters;
            mHaveSpaceFalseWaitMs += static_cast<std::uint64_t>(dsToMs(HAVE_SPACE_RETRY_DS));
#endif
            continue;
        }
        if (!ws->readyForData())
        {
            WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] readyForData=false -> continue [this = "
                      << this << "]";
            {
                ScopedUnlock unlock(lk);
                SteadyTime::sleep_ds(READY_FOR_DATA_RETRY_DS);
            }
#ifndef NDEBUG
            ++mReadyForDataFalseIters;
            mReadyForDataFalseWaitMs += static_cast<std::uint64_t>(dsToMs(READY_FOR_DATA_RETRY_DS));
#endif
            continue;
        }

        // fetch & enqueue next chunk (unlocks around disk I/O internally)
        // Synchronous preflight on the pool worker means sendChunk=false fires only when the
        // queue is genuinely empty, the pool is paused, or throttling rejected the chunk - not
        // once per file the way the posted-lambda preflight did.
        dstime retryAfterDs = 0;  // BACKLOG_EMPTY_RETRY_DS fallback below
        const auto wakeEpochBeforeSend = mImpl->workerWakeEpoch;
#ifndef NDEBUG
        const std::uint64_t chunkPrepStartMs = steadyMs();
#endif
        if (!sendChunk(ws.get(), *mImpl, &retryAfterDs))
        {
            WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] sendChunk=false -> continue [this = " << this
                      << "]";
            if (mPreflightPending)
            {
                // Preflight pending for at least one candidate file: wait on a
                // condition variable and wake immediately when preflight completes.
                static constexpr auto kPreflightPendingWait = std::chrono::milliseconds(100);
                mImpl->workerWakeCv.wait_for(
                    lk,
                    kPreflightPendingWait,
                    [&]
                    {
                        return mImpl->stopping() ||
                               mImpl->workerWakeEpoch != wakeEpochBeforeSend;
                    });
            }
            else
            {
                ScopedUnlock unlock(lk);
                SteadyTime::sleep_ds(retryAfterDs ? retryAfterDs : BACKLOG_EMPTY_RETRY_DS);
            }
#ifndef NDEBUG
            ++mBacklogEmptyIters;
            mBacklogEmptyMs +=
                static_cast<std::uint64_t>(retryAfterDs ? retryAfterDs : BACKLOG_EMPTY_RETRY_DS) *
                100;
#endif
            continue;
        }
#ifndef NDEBUG
        // Chunk-prep latency = entry-to-success of sendChunk (read + encrypt + queue).
        const std::uint64_t chunkPrepDtMs = steadyMs() - chunkPrepStartMs;
        mChunkPrepTotalMs += chunkPrepDtMs;
        if (chunkPrepDtMs > mChunkPrepMaxMs)
            mChunkPrepMaxMs = chunkPrepDtMs;
        ++mChunkPrepN;
#endif
    }

    if (ws && ws->readyState.load(std::memory_order_relaxed) != WsConn::ReadyState::CLOSED)
    {
        ScopedUnlock unlock(lk);
        ws->closeWS();
    }

    th->terminated = true;
    WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] END [lastQueueVersion=" << lastQueueVersion
              << "] [this = " << this << "]";
}

void WsPool::checkThreads()
{
    while (mActiveThreads.size() < mNumberOfConnections)
    {
        auto thr = std::make_unique<WsPoolThread>(this);
        mActiveThreads.push_back(std::move(thr));
    }
    while (mActiveThreads.size() > mNumberOfConnections)
    {
        mActiveThreads.back()->terminate = true;
        auto thr = std::move(mActiveThreads.back());
        mActiveThreads.pop_back();
        mExitingThreads.push_back(std::move(thr));
    }
}

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD
