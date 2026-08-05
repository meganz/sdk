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
#include <limits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
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
        out.totalCurlWsSendAcceptedBytes += c->mTotalCurlWsSendAcceptedBytes;
        out.partialFrameTornDownCount += c->mPartialFrameTornDownCount;
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
    // Peak concurrent in-flight connections, max across pools = the ACTUALLY-USED
    // flow count (read of an existing locked counter; test-stats path only).
    out.maxConnectionsWithInFlightSeen =
        std::max(out.maxConnectionsWithInFlightSeen, mMaxConnectionsWithInFlightSeen);
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
    // Queued retry first — but only entries whose owner is SENDABLE. S12 Cluster-B fix
    // (JENKINS_RCA_S12): a parked chunk whose owner was paused underneath used to be
    // popped and BUDGETED here every accrual cycle, then requeued by sendChunk's paused
    // check — livelocking this pool's fresh path/candidate re-selection (both sit behind
    // this loop) and starving sibling pools via the engine-global budget. Paused-owner
    // entries now stay parked at zero cost and the loop falls through to the fresh path.
    for (std::size_t ri = 0; ri < mToResend.size();)
    {
        chunk = mToResend[ri];
        // Discard stale resend entries whose target file is gone or whose byte range no
        // longer fits the current attempt (e.g. resetAttemptState rewound mHeadPos = 0
        // but the entry was queued under a previous attempt). Re-sending out-of-bounds
        // bytes would break the server-side sliding window; bytes the server already has
        // would be re-acked via opcode 2, but never-fitting ranges must be dropped here.
        WsUploadFile* const ufResend = findFile(chunk.fileno, impl);
        if (!ufResend || (chunk.len && chunk.pos + chunk.len > ufResend->size()))
        {
            WSUPLOAD_TRACE << "[WsPool::nextChunk] discard stale resend entry [pos=" << chunk.pos
                      << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno << "]";
            mToResend.erase(mToResend.begin() + static_cast<std::ptrdiff_t>(ri));
            continue;
        }
        if (ufResend->paused())
        {
            WSUPLOAD_TRACE << "[WsPool::nextChunk] resend entry owner paused -> keep parked "
                         "at zero budget cost [pos="
                      << chunk.pos << "] [fileno=" << chunk.fileno << "] [this = " << this
                      << "]";
            ++ri;
            continue;
        }
        if (!impl.consumeUploadBudget(static_cast<m_off_t>(chunk.len), retryAfterDs, this))
        {
            // Pool is alive but throttled. Keep it fresh so SERVERTIMEOUT does not
            // force an unnecessary refresh.
            mLastActive = impl.currentTime;
            return false;
        }
        mToResend.erase(mToResend.begin() + static_cast<std::ptrdiff_t>(ri));
        // Resend served: disarm the resend-stall watchdog (Cluster G, S13).
        mResendStallSinceDs = 0;
        mResendStallReprimed = false;
        WSUPLOAD_TRACE << "WsUpload: resending chunk pos=" << chunk.pos << " len=" << chunk.len
                  << " fileno=" << chunk.fileno;
        return true;
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

                if (!impl.consumeUploadBudget(static_cast<m_off_t>(chunk.len), retryAfterDs, this))
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
    // Loss-gated connection-count bump: a connection was lost and its in-flight chunks
    // are being requeued -- the authoritative runtime loss signal for this pool. Sticky
    // (never reset); consumed by lossBoostedConnLimitLocked().
    mLossObserved = true;
    WSUPLOAD_TRACE << "WsUpload: WS to " << mUrl << " lost; rescheduling " << ws->mChunksInFlight.size()
              << " in-flight chunks";
    // Tier 2 A whole-chunk-boundary rewind (loss-recovery). When the flag is on, re-queue
    // only the in-flight chunks NOT already fully acked by the server (mAckedIntervals),
    // at whole-chunk granularity so (pos,len) is invariant across requeue (safe on the
    // len-blind ack-match). A fully-acked chunk has already had its MAC applied (the same
    // opcode-1/7 that marked it acked also ran ChunkFingerprintMacUpdate::apply), so
    // skipping its re-send loses nothing and cannot under-credit. rangeFullyAcked only
    // returns true for a range fully covered by a single coalesced acked interval, so an
    // un-acked range is NEVER skipped (no silent corruption). mNumChunksInFlight is
    // decremented for ALL cleared records (acked or not) since all leave the in-flight set.
    //
    // When the flag is OFF this is byte-identical to the prior behavior (re-queue every
    // in-flight chunk).
    const bool ackedRewind = mImpl->mAckedChunkRewind;
    for (auto& p: ws->mChunksInFlight)
    {
        if (ackedRewind && p.first.len > 0)
        {
            const WsUploadFile* const uf = findFile(p.first.fileno, *mImpl);
            if (uf && uf->rangeFullyAcked(p.first.pos, p.first.pos + p.first.len))
            {
                WSUPLOAD_TRACE << "[WsPool::retryChunksOnTheWireLocked] skip already-acked chunk "
                                  "[pos="
                               << p.first.pos << "] [len=" << p.first.len
                               << "] [fileno=" << p.first.fileno << "] [this = " << this << "]";
                continue; // server already holds it; do not re-read + re-send
            }
        }
        mToResend.push_back(p.first);
    }
    mNumChunksInFlight -= static_cast<int>(ws->mChunksInFlight.size());
    ws->mChunksInFlight.clear();
}

void WsPool::retryChunkLocked(const WsChunk& chunk)
{
    // Loss-gated connection-count bump: a chunk is being requeued (server-rejected frame)
    // -- a runtime loss signal for this pool. Sticky (never reset); consumed by
    // lossBoostedConnLimitLocked().
    mLossObserved = true;
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

    // FIX B-2: the previous mUploadingFile shortcut dereferenced a non-owning raw alias
    // (eligibleInPool(mUploadingFile) -> f->mPool/paused()/continuingUpload()) before
    // confirming the object was live. The same hazards as findFile apply (stale alias
    // across applyRefreshedUrls swap/retire, free-after-unlock in Impl::remove). The
    // fileByNo scan below is identity-safe and returns the same in-pool eligible file
    // (mUploadingFile is always present in fileByNo when bound to this pool), so drop the
    // shortcut and resolve only through the owning index.

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

dstime WsPool::reconnectBackoffDsLocked(const int retryCount) const
{
    // Capped-exponential base: CONNRETRYINTERVAL doubled per consecutive failure,
    // clamped at min(retryCount,3) doublings (8x) and at CONNRETRYMAXINTERVAL. On
    // the clean link this is never reached (connectWS succeeds first), so it is a
    // failure-only no-op (root_cause.md S3c).
    const int shift = std::min(retryCount < 0 ? 0 : retryCount, 3);
    dstime base = CONNRETRYINTERVAL << shift;
    if (base > CONNRETRYMAXINTERVAL)
    {
        base = CONNRETRYMAXINTERVAL;
    }

    // +0..50% jitter (mirrors BackoffTimer::backoff()'s base + (base/2)*frac shape,
    // src/backofftimer.cpp). Lock-free: a thread_local engine, seeded once per
    // worker thread, so this never touches uploadMutex / the engine PrnGen.
    thread_local std::minstd_rand rng{std::random_device{}()};
    const double frac =
        static_cast<double>(rng()) / static_cast<double>(std::minstd_rand::max());
    const dstime jitter = static_cast<dstime>((static_cast<double>(base) / 2.0) * frac);
    return base + jitter;
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

void WsPool::purgeQueuedResendForRangeLocked(const std::uint32_t fileno,
                                             const m_off_t pos,
                                             const int len)
{
    // Range-scoped twin of purgeFileLocked's mToResend pass: erase only the QUEUED
    // resend whose (fileno,pos,len) matches exactly (the chunk was queued whole by
    // retryChunksOnTheWireLocked, so the range is identical). Deliberately does NOT
    // touch mChunksInFlight (the per-conn ack firewall) or mBytesConfirmed. If no match
    // is found (the chunk was already re-sent / never queued), this is a harmless no-op
    // -- identical to today's behavior (the server dedup re-fires harmlessly).
    for (auto it = mToResend.begin(); it != mToResend.end();)
    {
        if (it->fileno == fileno && it->pos == pos && it->len == len)
        {
            it = mToResend.erase(it);
        }
        else
        {
            ++it;
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
            // S12 Cluster-B fix: the chunk never reached the wire — refund its budget so
            // pool-mates are not charged for the paused-underneath race.
            impl.refundUploadBudget(static_cast<m_off_t>(chunk.len));
            return false;
        }
        if (uf->hasFailed())
        {
            // markFailed() (e.g. activateoverquota walking PUT transfers when the
            // account goes RED) can fail the attempt from the client thread between
            // readData's generation check and this point. Unlike uploadFailed() it
            // keeps the file pooled, so the inPool/aborted/paused checks above do
            // not catch it: drop the chunk; the attempt reset already cleared the
            // acked intervals, so a future retry re-sends from scratch.
            WSUPLOAD_TRACE << "[WsPool::sendChunk] drop chunk after read (failed underneath) [pos="
                           << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno
                           << "] [this = " << this << "]";
            return false;
        }
        assert(uf->isUploading() && "invariant: upload must be active after successful read "
                                    "with inPool, !aborted, !paused, !hasFailed checks passing; "
                                    "the mWorkGeneration match inside readData guarantees the "
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
                // S12 Cluster-B fix: never reached the wire — refund (see post-read site).
                impl.refundUploadBudget(static_cast<m_off_t>(chunk.len));
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

int WsPool::coldStartHandshakeCapLocked() const
{
    // Default: keep fix #2's de-convoy cap (2 = one in-flight + one warm spare).
    // Pinned pools never narrow (they also bypass the gate that calls this; this
    // short-circuit is belt-and-suspenders so the helper is correct in isolation).
    if (mPinned || !mImpl || !mImpl->mSmallFileColdStart)
    {
        return COLDSTART_HANDSHAKE_CONNS;
    }

    // fu8 S2: never narrow to 1 cold connection when conns<=2 (serializes under loss -> c2 stall)
    if (mImpl->poolConnectionLimit() <= 2)
    {
        return static_cast<int>(mImpl->poolConnectionLimit());
    }

    // Narrow to 1 ONLY for a genuinely-isolated single small file. Each clause below is
    // a revert-to-2 trigger that is re-checked on every worker loop pass (the gate calls
    // this fresh each time), so a burst, a large file, or ANY in-flight/resend (including
    // the first dropped frame under loss) immediately restores cap=2 and keeps the S3
    // convoy closed:
    //   - mNumChunksInFlight == 0 && mToResend.empty(): no send/resend activity yet, so
    //     there is no convoy to throttle and no warm spare is needed for recovery.
    //   - exactly ONE queued-eligible file for this size class. We count via a bounded
    //     fileList scan (cap=2 early-exit), NOT mNumPoolFiles -- mNumPoolFiles is bound
    //     too late (incremented in setPool()/sendChunk AFTER the handshake), so at the
    //     cold gate it reads 0 even when N burst files are queued; gating on it would fire
    //     cap=1 on the first file of a burst and re-open the convoy.
    //   - that single file's ACTUAL size() <= kSingleConnFileSizeCeiling. mMaxFileSize is
    //     the USC size-class CEILING (urlmaxsize.second), NOT the file size, so it is
    //     unusable as the discriminator (every small file routes to the same small pool).
    if (mNumChunksInFlight != 0 || !mToResend.empty())
    {
        return COLDSTART_HANDSHAKE_CONNS;
    }

    m_off_t firstEligibleSize = 0;
    const unsigned eligibleCount = mImpl->eligibleFileCountForPoolCappedLocked(
        mMinFileSize,
        mMaxFileSize,
        mPinned ? &mUrl : nullptr,
        this,
        /*cap*/ static_cast<unsigned>(COLDSTART_HANDSHAKE_CONNS),
        &firstEligibleSize);

    const bool singleSmallFile = (eligibleCount == 1) && (firstEligibleSize > 0) &&
                                 (firstEligibleSize <= kSingleConnFileSizeCeiling);
    return singleSmallFile ? 1 : COLDSTART_HANDSHAKE_CONNS;
}

unsigned char WsPool::lossBoostedConnLimitLocked(const UploadEngine::Impl& impl) const
{
    // Pinned pools (session-URL-affined failover pools, not size-class upload pools) never
    // boost -- they keep the default limit.
    if (mPinned)
    {
        return impl.poolConnectionLimit();
    }

    // DATASET connection-count bump (A24, SDK-5360 fu8 Session 5). A pool carrying >= 2 files
    // sets its connection-count CEILING to K (lossBoostedDatasetConnLimit, default 32;
    // runtime-overridable via MEGA_WS_DATASET_CONN_LIMIT). The FLOW fix: datasets trail develop
    // purely on independent-flow count (per-conn goodput is loss-capped; only N moves the
    // aggregate -- GOAL3_ROOTCAUSE_VERDICT_v2).
    //
    // This helper returns only the CEILING. How the pool APPROACHES it is decided in
    // WsPoolMgr::checkPools: with the goodput-saturation GATE on (MEGA_WS_DATASET_CONN_GATE,
    // default) the pool RAMPS toward K only while aggregate goodput keeps rising under full
    // backpressure (so clean links stay at the low default and only loss-limited datasets
    // widen -- WsPool::runGoodputGateLocked); with the gate OFF checkPools JUMPS straight to K
    // (the historical unconditional bump). Either way this CEILING is unchanged.
    //
    // NOT loss-gated (and deliberately so): the WS engine CANNOT observe sub-connection packet
    // loss -- TCP retransmits hide 5% loss below the socket, so no WS connection drop, no chunk
    // requeue, and mLossObserved never trips (S5 smoke: 0 requeues, mLossObserved=0 in ~all
    // samples under loss5 while throughput was still crippled). A loss-gated bump therefore
    // never engages against the actual target scenario. develop's own HTTP path is
    // unconditionally concurrent (connections[PUT] per slot x pooling), so matching that model
    // is correct. mNumPoolFiles reliably counts files bound to THIS pool (the instantaneous
    // eligibility scan under-counts: non-pinned pools skip session-URL-hinted files -> it read
    // 0-1 for a genuine dataset). RSS stays bounded to ~K TOTAL conns by the global ceiling in
    // WsPoolMgr::checkPools (lossBoostedGlobalConnCeiling), so several concurrent size-class
    // pools cannot each reach K. Gated by MEGA_WS_DATASET_CONN_BUMP for A/B in one binary; when
    // off, byte-identical to the shipped engine (falls through to the shipped lone-small path).
    // SDK-5360 fu8 Session 7, Lever C: the single-file ramp is env-gated OFF by default. When
    // MEGA_WS_SINGLEFILE_CONN_BUMP=1 (impl.mSingleFileConnBump), a single-file pool
    // (mNumPoolFiles==1) also gets the boosted ceiling and rides the goodput gate
    // (WsPool::runGoodputGateLocked / WsPoolMgr::checkPools), which withholds the extra conns on
    // a clean link and only widens a genuinely loss-limited single upload. One file's chunks are
    // split across pool connections (shared mUploadingFile head cursor in nextChunk), so the
    // extra flows are usable. Default (bump off) keeps the historical mNumPoolFiles>=2 gate, so
    // this branch is byte-identical to today unless the env opts in.
    // The instantaneous count is OR'd with the sticky mDatasetSeen latch (fu8 S8): the client
    // dispatcher keeps only ~30 s x speed of transfers outstanding, so on a slow link a genuine
    // dataset is trickle-fed and mNumPoolFiles flaps 1<->2 between refills. Without the latch
    // the ceiling de-arms on every flap and the goodput-gate controller starves (its windows
    // stall past the stale reseed; the S7 qaexact 4.6-min engage plausibly includes this).
    // GATE=0 behavior is unchanged by the latch: the jump branch only ever RAISES conns, and it
    // already kept the peak across flaps (conns > base fails the < targetConnLimit test).
    if (impl.mDatasetConnBump &&
        (mNumPoolFiles >= (impl.mSingleFileConnBump ? 1 : 2) || mDatasetSeen))
    {
        return std::max<unsigned char>(impl.poolConnectionLimit(),
                                       impl.lossBoostedDatasetConnLimit());
    }

    // --- Shipped lone-small-file boost, UNCHANGED (loss-gated on a REAL drop/requeue). ---
    // A single small file that hit an actual connection drop / server-rejected frame
    // (mLossObserved set at the two requeue sites) widens toward kLossBoostedConnLimit (8) so
    // the straggler uploads on ~8 flows like develop. This path is unchanged from the shipped
    // engine: it fires on genuine drops (it just does not fire under mild TCP-absorbed loss --
    // same as before). No boost unless mLossConnBump is on and this pool observed loss.
    if (!impl.mLossConnBump || !mLossObserved)
    {
        return impl.poolConnectionLimit();
    }
    m_off_t firstEligibleSize = 0;
    const unsigned eligibleCount = impl.eligibleFileCountForPoolCappedLocked(
        mMinFileSize,
        mMaxFileSize,
        /*requiredSessionUrl*/ nullptr, // non-pinned here (mPinned returned above)
        this,
        /*cap*/ 2u,
        &firstEligibleSize);
    const bool loneSmallFile = (eligibleCount <= 1) && (firstEligibleSize > 0) &&
                               (firstEligibleSize <= kSingleConnFileSizeCeiling);
    return loneSmallFile ?
               std::max<unsigned char>(impl.poolConnectionLimit(), kLossBoostedConnLimit) :
               impl.poolConnectionLimit();
}

// Goodput-saturation gate v2 ramp controller (SDK-5360 QCT-K, fu8 S8). See the header
// declaration for the contract and analysis/GOAL1_gate_v2_design.md (followup8_QA_7) for the
// design rationale. Replaces the S7 hill-climb whose two audit-proven weaknesses were:
//  - L1/L3: WIDEN required EVERY open conn backpressured at ONE once-per-window sampling
//    instant -- two stacked coincidences that cost a trace-proven ~4.6-min engage delay on
//    qaexact and froze the ramp at 16-of-30 mid-climb (hold-nobp after one misaligned window).
//  - L2: the first probe at any level was unconditional and a retreat only paused 5 windows,
//    so a clean network-capped link re-probed forever (43 widens -> 22-conn plateau).
// Runs UNDER uploadMutex (checkPools holds it). All controller state + mConfirmedBytesTotal are
// plain (uploadMutex-guarded, HR23); the ONE cross-thread input is per-conn backpressure, read
// via the relaxed atomics inside countOpenAndBackpressuredLocked (same two atomics the old
// all-conns helper read). Engage keys ONLY on the send-buffer backpressure quorum -- a signal
// measured absent during MassNotify (337 hold-nobp windows, 0 widens) -- NEVER on loss/requeue
// counters (a watchdog force-reconnect increments those: coupling them here would let a
// watchdog fire drive a widen), queue depth or file count (audit: queue depth does not
// discriminate the MassNotify pass/hang; conn count / enqueue rate does).
void WsPool::runGoodputGateLocked(const UploadEngine::Impl& impl, const unsigned char ceilingIn)
{
    const dstime now = impl.currentTime;
    const dstime windowDs = impl.gateWindowDs();

    // Window-unit constants:
    //  - kGateBackoffBaseWindows: first failed-probe backoff == S7's retreat cooldown (the
    //    back-compat anchor); doubles per consecutive failure up to mGateRetreatMaxWindows.
    //  - kGateStaleWindows: an elapsed window longer than this means the pool sat idle/paused.
    //    Safe against long backoffs BECAUSE the sampling window is strictly uniform (always +W;
    //    a backoff inhibits probes, it no longer stretches the window) -- the FLAW-2 decoupling.
    //  - kGateBestImproveMinPct: knee memory updates only on a STRICT improvement, so noise
    //    cannot creep the remembered best upward.
    constexpr dstime kGateBackoffBaseWindows = 5;
    constexpr dstime kGateStaleWindows = 12;
    constexpr double kGateBestImproveMinPct = 2.0;

    const auto satInc = [](std::uint8_t& v)
    {
        if (v != std::numeric_limits<std::uint8_t>::max())
            ++v;
    };

    // Device-scaled effective ceiling, IN-GATE so the GATE=0 jump-to-K path stays byte-identical
    // on every platform (mobile base 3 -> 12 at the default x4, mirroring desktop 32 = 4x8 and
    // the MAX_RAIDTRANSFERS_FOR_MOBILE precedent; 0 disables the mult).
    const unsigned base = impl.poolConnectionLimit();
    unsigned ceiling = ceilingIn;
    if (impl.mGateCeilingMult)
    {
        ceiling = std::clamp<unsigned>(std::min<unsigned>(ceilingIn, base * impl.mGateCeilingMult),
                                       base,
                                       ceilingIn);
    }

    // A. EVERY-TICK sticky quorum sampling (~2 Hz), BEFORE the once-per-window return. This is
    // the fix for the "sampled at one instant" half of L1/L3: a conn backpressured at ANY tick
    // this window counts, and the quorum needs only mGateBpQuorumPct% of open conns (default 50)
    // instead of 100%. The test hook overrides only the controller's VIEW of backpressure; the
    // real send path is untouched.
    unsigned open = 0;
    unsigned bp = 0;
    countOpenAndBackpressuredLocked(open, bp);
    DEBUG_TEST_HOOK_WS_GATE_BP_SAMPLE(static_cast<const void*>(this), open, bp);
    satInc(mGateBpTicks);
    if (open > 0 && bp * 100u >= open * impl.mGateBpQuorumPct)
        satInc(mGateBpQuorumTicks);

    // B. First observation (or after a reset): seed the measurement anchors; decide nothing yet.
    if (!mGateLastSampleDs)
    {
        resetGateControllerStateLocked();
        mGateLastSampleDs = now;
        mGateLastSampleBytes = mConfirmedBytesTotal;
        mGateNextAdjustDs = now + windowDs;
        LOG_debug << "[GoodputGate] pool=" << static_cast<const void*>(this)
                  << " files=" << mNumPoolFiles << " SEED conns="
                  << static_cast<unsigned>(mNumberOfConnections) << " base=" << base
                  << " ceiling=" << ceiling << "/" << static_cast<unsigned>(ceilingIn)
                  << " windowDs=" << windowDs;
        return;
    }

    // C. Uniform once-per-window pacing: act at most once per window, NOT on every checkPools
    // tick. ALWAYS one window (probe backoff is a separate timer checked at the widen decision,
    // NOT a stretched window) so the sampler, trim debounce and staleness stay well-defined
    // during a long backoff.
    if (SteadyTime::difference(now, mGateNextAdjustDs) < 0)
        return;

    const dstime elapsedDs = SteadyTime::difference(now, mGateLastSampleDs);

    // D. Stale window -- a TRUE idle gap now that backoffs no longer stretch the window (idle
    // gaps are >= POOLCONNKEEPALIVE, far above this bound): re-seed everything including knee
    // memory and backoff so a resumed pool re-measures from a clean slate; decide nothing.
    if (elapsedDs <= 0 || elapsedDs > windowDs * kGateStaleWindows)
    {
        resetGateControllerStateLocked();
        mGateLastSampleDs = now;
        mGateLastSampleBytes = mConfirmedBytesTotal;
        mGateNextAdjustDs = now + windowDs;
        return;
    }

    // E. Window measurement: aggregate confirmed goodput over the window, bytes/second (double:
    // once-per-window, cold path). windowBp is the sticky-OR of the per-tick quorum samples.
    const std::uint64_t deltaBytes = mConfirmedBytesTotal - mGateLastSampleBytes;
    const double elapsedSec = static_cast<double>(elapsedDs) / static_cast<double>(kDsPerSecond);
    double currentGoodput = static_cast<double>(deltaBytes) / elapsedSec;
    const std::uint32_t windowEvents = mGateAckEvents - mGateLastSampleEvents;
    const std::uint32_t probeEvents = mGateAckEvents - mGateProbeStartEvents;
    // Level-session baseline (A1c): average since the conn level was entered -- same evidence
    // discipline as the probe side, immune to single-window lump noise in either direction.
    if (!mGateLevelStartDs)
    {
        mGateLevelStartDs = mGateLastSampleDs;
        mGateLevelStartBytes = mGateLastSampleBytes;
        mGateLevelStartEvents = mGateLastSampleEvents;
    }
    const std::int32_t levelElapsedDs = SteadyTime::difference(now, mGateLevelStartDs);
    const std::uint32_t levelEvents = mGateAckEvents - mGateLevelStartEvents;
    const double levelGoodput =
        (levelElapsedDs > 0) ?
            static_cast<double>(mConfirmedBytesTotal - mGateLevelStartBytes) /
                (static_cast<double>(levelElapsedDs) / static_cast<double>(kDsPerSecond)) :
            0.0;
    bool goodputOverridden = false;
    DEBUG_TEST_HOOK_WS_GATE_GOODPUT(static_cast<const void*>(this),
                                    static_cast<unsigned>(mNumberOfConnections),
                                    currentGoodput,
                                    goodputOverridden);
    const bool windowBp = (mGateBpQuorumTicks > 0);
    // Scaled gain bar (amendment A3): adding `step` conns to N ideally gains step/N of the
    // aggregate, so the required gain is half-of-ideal, capped by GAIN_PCT (back-compat at the
    // base: min(5%, 6.25%) = 5% at 8 conns) and floored at 1%. A flat bar past N~20 is
    // mathematically unreachable and parked the uncapped-loss climb at ~14 conns in a
    // retreat/backoff sawtooth; a capped link's true gain is ~0, below ANY bar, so the
    // clean-capped discrimination is unaffected.
    const double halfIdealPct =
        (mGateProbing && mGateProbeFromConns) ?
            100.0 * static_cast<double>(mGateProbeStep) /
                (2.0 * static_cast<double>(mGateProbeFromConns)) :
            static_cast<double>(impl.mGateGainPct);
    const double requiredGainPct = std::clamp(
        halfIdealPct, 1.0, static_cast<double>(std::max<unsigned>(1u, impl.mGateGainPct)));
    const double gainMultiplier = 1.0 + requiredGainPct / 100.0;
    const unsigned step = impl.mGateStep ? impl.mGateStep : 1u;
    const unsigned connBefore = static_cast<unsigned>(mNumberOfConnections);
    const bool wasProbing = mGateProbing;

    // Baseline-capture + widen in one place so the probe anchors are always consistent: the
    // baseline is the last full window's goodput at the CURRENT level, and the probe horizon is
    // judged on the average since the widen (noise must sustain the gain across the horizon).
    const auto resetLevelAnchors = [&]()
    {
        mGateLevelStartDs = now;
        mGateLevelStartBytes = mConfirmedBytesTotal;
        mGateLevelStartEvents = mGateAckEvents;
    };
    const auto startProbe = [&](const unsigned s, const double baseline)
    {
        mGateGoodputBeforeIncrease = baseline;
        const unsigned to = std::min<unsigned>(connBefore + s, ceiling);
        setPoolNumConn(static_cast<unsigned char>(to));
        mGateProbing = true;
        mGateProbeStartDs = now;
        mGateProbeStartBytes = mConfirmedBytesTotal;
        mGateProbeStartEvents = mGateAckEvents;
        mGateProbeFromConns = static_cast<std::uint8_t>(connBefore);
        mGateProbeStep = static_cast<std::uint8_t>(to - connBefore);
        resetLevelAnchors(); // the probe's level IS the new level
    };

    const char* action = "hold-nobp";
    double probeGoodput = 0.0;

    // Probe horizon completion (amendment A1): time horizon AND an evidence floor. Server acks
    // land in whole-chunk lumps, so a wall-clock horizon alone judges on 0-2 samples at low
    // bandwidth (the cleanNet8m smoke chain-confirmed probes against ZERO baselines all the way
    // to the ceiling). The horizon auto-extends until >= mGateMinEvents confirm events arrive,
    // capped at kGateProbeEvidenceCapWindows. The cap is GENEROUS (45 windows) and at the cap
    // the judge accepts PROPORTIONAL evidence (>= minEvents/4, floor 2): a slow-but-alive lossy
    // link accumulates its events in ~30-40 s and gets a fair ruling (a 10-window cap failed
    // EVERY probe below ~0.8 MB/s -- smoke 2 froze loss5Net4m at base and lost ~19%
    // throughput), while a genuinely parked probe (< 2 events in 45 s) still FAILS -- it never
    // confirms on noise. A hook-driven synthetic goodput bypasses the floor (the synthetic
    // value IS the evidence).
    constexpr dstime kGateProbeEvidenceCapWindows = 45;
    const std::int32_t probeElapsedTotalDs =
        mGateProbing ? SteadyTime::difference(now, mGateProbeStartDs) : 0;
    const bool probeAtEvidenceCap =
        mGateProbing &&
        probeElapsedTotalDs >= static_cast<std::int32_t>(windowDs * kGateProbeEvidenceCapWindows);
    const bool probeHorizonDone =
        mGateProbing &&
        probeElapsedTotalDs >= static_cast<std::int32_t>(windowDs * impl.mGateProbeWindows) &&
        (goodputOverridden || probeEvents >= impl.mGateMinEvents || probeAtEvidenceCap);

    // F. Decision.
    if (probeHorizonDone)
    {
        // Judge the +step on its horizon AVERAGE against the baseline. The gain test is the
        // clean-vs-loss discriminator: a clean bandwidth-limited link does NOT gain when a flow
        // is added (the flow just splits the same uplink) -> retreat; a loss-limited link DOES
        // gain (each flow is an independent cwnd) -> keep climbing.
        const dstime probeElapsedDs = SteadyTime::difference(now, mGateProbeStartDs);
        const double probeElapsedSec =
            static_cast<double>(probeElapsedDs) / static_cast<double>(kDsPerSecond);
        probeGoodput =
            static_cast<double>(mConfirmedBytesTotal - mGateProbeStartBytes) / probeElapsedSec;
        DEBUG_TEST_HOOK_WS_GATE_GOODPUT(static_cast<const void*>(this),
                                        connBefore,
                                        probeGoodput,
                                        goodputOverridden);
        // CONFIRM needs (a) the gain, (b) a MEANINGFUL baseline (a zero baseline confirms
        // nothing -- that was the vacuous-confirm hole), and (c) sufficient probe evidence:
        // the full minEvents floor normally, or the proportional floor when the generous
        // evidence cap expired first (slow-but-alive links get a fair ruling on what arrived).
        const std::uint32_t evidenceFloor =
            probeAtEvidenceCap ? std::max<std::uint32_t>(2u, impl.mGateMinEvents / 4u) :
                                 impl.mGateMinEvents;
        if (probeGoodput >= mGateGoodputBeforeIncrease * gainMultiplier &&
            (goodputOverridden ||
             (mGateGoodputBeforeIncrease > 0.0 && probeEvents >= evidenceFloor)))
        {
            // CONFIRMED gain. Update the knee memory on a STRICT improvement only, then CHAIN
            // the next widen while pressure persists (no mid-ramp freeze: a quorum-false window
            // after a confirmed probe KEEPS the level -- hold-gained -- and the climb resumes
            // without debounce the moment quorum returns).
            mGateFailedProbes = 0;
            if (probeGoodput > mGateBestGoodput * (1.0 + kGateBestImproveMinPct / 100.0))
            {
                mGateBestGoodput = probeGoodput;
                mGateBestConns = static_cast<std::uint8_t>(connBefore);
            }
            if (windowBp && connBefore < ceiling && (goodputOverridden || windowEvents > 0))
            {
                // Chained baseline = the horizon AVERAGE just confirmed (>= minEvents of
                // evidence), not the single-window rate -- the most reliable number we have.
                // A2: each confirm doubles the next chained step (evidence-paced probes are
                // slow on lossy links; a few judged geometric steps replace many +1s), clamped
                // to a hard cap and the remaining headroom.
                constexpr unsigned kGateChainStepCap = 8;
                const unsigned chained = std::min<unsigned>(
                    {step << std::min<unsigned>(mGateChainStep, 3u),
                     kGateChainStepCap,
                     ceiling - connBefore});
                if (mGateChainStep < 3)
                    ++mGateChainStep;
                startProbe(std::max(1u, chained), probeGoodput);
                action = "widen";
            }
            else
            {
                // Keep the level; carry the probe's evidence-rich measurement over as the
                // settled level session (A1c) so a later chain resume judges against it.
                mGateProbing = false;
                mGateChainStep = 0;
                mGateLevelStartDs = mGateProbeStartDs;
                mGateLevelStartBytes = mGateProbeStartBytes;
                mGateLevelStartEvents = mGateProbeStartEvents;
                action = "hold-gained";
            }
        }
        else
        {
            // FAILED probe: revert to the remembered knee (the best conn count that ever
            // strictly improved goodput), not merely -step -- this kills the clean-capped
            // up-ratchet -- and inhibit further probes with a REAL exponential backoff
            // (5,10,20,40,80,160,300 windows; a separate timer, so trim keeps running).
            // A5: a failed probe undoes ONLY its own step (back to the level it stepped from).
            // Retreat-to-knee pulled the pool from ~23 to ~14 whenever a stale lucky-burst
            // bestGoodput made the knee unbeatable (113 retreats/run on uncapped loss5 -- the
            // churn, not the conn policy, was the throughput tax). The knee stays a FLOOR
            // (never retreat below it), and on capped links the probe-from level IS ~base, so
            // the L2 behavior is unchanged.
            const unsigned probeFrom = mGateProbeFromConns ? mGateProbeFromConns : base;
            const unsigned kneeFloor = std::max<unsigned>(base, mGateBestConns);
            const unsigned target = std::max(kneeFloor, std::min(probeFrom, connBefore));
            if (target < connBefore)
                setPoolNumConn(static_cast<unsigned char>(target));
            mGateProbing = false;
            mGateChainStep = 0;
            resetLevelAnchors();
            satInc(mGateFailedProbes);
            const dstime backoffWindows = std::min<dstime>(
                kGateBackoffBaseWindows
                    << std::min<unsigned>(static_cast<unsigned>(mGateFailedProbes) - 1u, 6u),
                impl.mGateRetreatMaxWindows);
            mGateProbeBackoffUntilDs = now + windowDs * backoffWindows;
            action = (target < connBefore) ? "retreat" : "retreat-atfloor";
        }
    }
    else if (mGateProbing)
    {
        // Mid-horizon window: the probe keeps measuring; no decision this window.
        action = "probe-wait";
    }
    else if (windowBp && connBefore < ceiling &&
             SteadyTime::difference(now, mGateProbeBackoffUntilDs) >= 0)
    {
        // FAST-ENGAGE from base after a short debounce (anti-stampede), or RESUME the climb
        // without debounce when already above base (the L3 fix's second half).
        if (connBefore == base)
            satInc(mGateEngageStreak);
        if (connBefore == base && mGateEngageStreak < impl.mGateEngageWindows)
        {
            action = "hold-debounce";
        }
        else if (!goodputOverridden &&
                 levelEvents < std::max<std::uint32_t>(2u, impl.mGateMinEvents / 4u))
        {
            // Not enough LEVEL-session evidence for a meaningful baseline yet (A1c): a probe
            // started now could only be judged against lump noise. Hold without burning a
            // probe/backoff; evidence accumulates and the probe starts within a few windows.
            action = "hold-lowsig";
        }
        else
        {
            startProbe(step, goodputOverridden ? currentGoodput : levelGoodput);
            action = "widen";
        }
    }
    else
    {
        action = (connBefore >= ceiling) ? "hold-ceiling" : (windowBp ? "hold-backoff" : "hold-nobp");
    }

    // G. TRIM -- independent of the probe backoff (runs whenever the pool is above base and the
    // window was calm), so capacity returns after a loss episode instead of parking at the peak
    // (S7 held the peak until pool retirement). Halve toward base; calm is a regime change, so
    // clear the failed-probe backoff; reaching base disengages and forgets the knee.
    // (Live count, not the pre-decision snapshot: a retreat in F this same window must not
    // leave the trim math on a stale level.)
    const unsigned connNow = static_cast<unsigned>(mNumberOfConnections);
    if (!windowBp)
    {
        mGateEngageStreak = 0;
        // The resend-backlog guard keeps trim OUT of steady loss: under active loss the resend
        // queue is rarely empty (chunks bounce constantly), so quorum-false lulls do not shed
        // the flows the loss still needs; after a genuine bad->clean transition the resends
        // drain and trim proceeds (the transition cells' proven behavior).
        if (impl.mGateTrimWindows && connNow > base && !mGateProbing && mToResend.empty())
        {
            satInc(mGateNoBpWindows);
            if (mGateNoBpWindows >= impl.mGateTrimWindows)
            {
                const unsigned down = base + (connNow - base) / 2;
                setPoolNumConn(static_cast<unsigned char>(down));
                resetLevelAnchors();
                mGateNoBpWindows = 0;
                mGateFailedProbes = 0;
                mGateChainStep = 0;
                mGateProbeBackoffUntilDs = 0;
                if (down == base)
                {
                    mGateBestGoodput = 0.0;
                    mGateBestConns = 0;
                }
                action = "trim";
            }
        }
    }
    else
    {
        mGateNoBpWindows = 0;
    }

    // H. One greppable line per window (superset of the S7 line -- extractors keyed on
    // "[GoodputGate]"/action/conns keep working) so the ramp/hold/trim shape is VERIFIABLE from
    // the bench PID trace. LOG_debug, not WSUPLOAD_TRACE (which may be compiled out).
    const dstime backoffLeftDs =
        (mGateProbeBackoffUntilDs && SteadyTime::difference(mGateProbeBackoffUntilDs, now) > 0) ?
            SteadyTime::difference(mGateProbeBackoffUntilDs, now) :
            0;
    LOG_debug << "[GoodputGate] pool=" << static_cast<const void*>(this)
              << " files=" << mNumPoolFiles << " action=" << action
              << " conns=" << connBefore << "->" << static_cast<unsigned>(mNumberOfConnections)
              << " base=" << base
              << " ceiling=" << ceiling << "/" << static_cast<unsigned>(ceilingIn)
              << " quorumTicks=" << static_cast<unsigned>(mGateBpQuorumTicks) << "/"
              << static_cast<unsigned>(mGateBpTicks) << " open=" << open << " bp=" << bp
              << " windowBp=" << windowBp
              << " engageStreak=" << static_cast<unsigned>(mGateEngageStreak)
              << " noBpW=" << static_cast<unsigned>(mGateNoBpWindows)
              << " failedProbes=" << static_cast<unsigned>(mGateFailedProbes)
              << " backoffLeftDs=" << backoffLeftDs << " best=("
              << static_cast<unsigned>(mGateBestConns) << "," << mGateBestGoodput << ")"
              << " goodputBps=" << currentGoodput
              << " baselineBps=" << mGateGoodputBeforeIncrease << " probeBps=" << probeGoodput
              << " events=" << windowEvents << "/" << probeEvents << "/" << levelEvents
              << " levelBps=" << levelGoodput
              << " chainStep=" << static_cast<unsigned>(mGateChainStep)
              << " overridden=" << goodputOverridden
              << " wasProbing=" << wasProbing << " gainX=" << gainMultiplier
              << " reqPct=" << requiredGainPct
              << " elapsedDs=" << elapsedDs;

    // Advance the measurement window (STRICTLY uniform: always +W) and reset the tick samples.
    mGateLastSampleDs = now;
    mGateLastSampleBytes = mConfirmedBytesTotal;
    mGateLastSampleEvents = mGateAckEvents;
    mGateNextAdjustDs = now + windowDs;
    mGateBpTicks = 0;
    mGateBpQuorumTicks = 0;
}

void WsPool::poolWorkerThread(WsPoolThread* th)
{
    // S12 Cluster-E fix: live-worker accounting for the bounded locallogout quiesce.
    ++mImpl->mLiveWorkerThreads;
    const auto liveWorkerGuard = [](std::atomic<int>* c)
    {
        return std::unique_ptr<std::atomic<int>, void (*)(std::atomic<int>*)>(
            c,
            [](std::atomic<int>* cc)
            {
                --*cc;
            });
    }(&mImpl->mLiveWorkerThreads);

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

        // Ack-stall watchdog force-reconnect (fu8 S6): WsPoolMgr::checkPools set mForceReconnect
        // on this conn after its server acks went silent past ACKSTALLTIMEOUT while it held
        // in-flight chunks. Honour it here, right beside the disconnectEpoch close, reusing the
        // normal close path so un-acked chunks are re-queued safely (acked bytes preserved via
        // mAckedIntervals). exchange() clears the flag so one request maps to exactly one close;
        // the reconnect then re-handshakes on the next loop pass like any other closed conn.
        if (ws->mForceReconnect.exchange(false, std::memory_order_relaxed) &&
            ws->readyState.load(std::memory_order_relaxed) != WsConn::ReadyState::CLOSED)
        {
            WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] ack-stall force-reconnect -> closeWS() "
                              "[this = " << this << "] [ws = " << ws.get() << "]";
            ScopedUnlock unlock(lk);
            ws->closeWS();
        }

        if (ws->readyState.load(std::memory_order_relaxed) == WsConn::ReadyState::CLOSED)
        {
            // Cold-start de-convoy gate (fix #2, root_cause.md S3b). uploadMutex (lk)
            // is held here. A worker may handshake iff the pool already has >=1 OPEN
            // connection (warm refill path) OR fewer than the cold-start cap workers are
            // currently handshaking (cold path). The cap is normally COLDSTART_HANDSHAKE_CONNS
            // (2) but narrows to 1 for a genuinely-isolated single small file (candidate 3c,
            // coldStartHandshakeCapLocked()): re-evaluated every loop pass, so a 2nd queued
            // file / large file / any in-flight/resend (incl. the first dropped frame under
            // loss) reverts it to 2 and keeps the de-convoy + >=20%-loss S3 convoy closed.
            // Otherwise park on the Lever-F workerWakeCv (releasing lk while parked) and
            // re-loop. Pinned pools bypass entirely so InvalidPinned* failover timing is
            // unchanged (and coldStartHandshakeCapLocked() never narrows a pinned pool).
            if (!mPinned && !anyOpenConnLocked() &&
                mConnectingCount >= coldStartHandshakeCapLocked())
            {
                const auto wakeEpochBeforeGate = mImpl->workerWakeEpoch;
                mImpl->workerWakeCv.wait_for(
                    lk,
                    std::chrono::milliseconds(200),
                    [&]
                    {
                        return th->terminate || mImpl->stopping() ||
                               mImpl->workerWakeEpoch != wakeEpochBeforeGate;
                    });
                continue; // re-evaluate: another worker may now be OPEN
            }
            // Reserve a cold-start slot. The write happens with lk held (this thread);
            // the matching decrement happens after the ScopedUnlock dtor re-locks lk.
            // Single-decrement invariant: incremented on exactly one branch (non-pinned,
            // was CLOSED, admitted), decremented exactly once by the same worker below.
            const bool reservedColdStart = !mPinned;
            if (reservedColdStart)
            {
                ++mConnectingCount;
            }
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
            // lk is re-held here (ScopedUnlock dtor). Release the cold-start slot and
            // wake parked workers so the gate is re-evaluated promptly instead of after
            // the 200ms poll (a connection just opened or failed).
            if (reservedColdStart && mConnectingCount > 0)
            {
                --mConnectingCount;
            }
            mImpl->notifyWorkersLocked(); // wsupload_engine.h, lk held

            if (!ok)
            {
                const dstime nowDs = SteadyTime::ds();
                if (!firstConnectFailureDs)
                {
                    firstConnectFailureDs = nowDs;
                }

                if (!retryCount++)
                {
#ifndef NDEBUG
                    mGateRetryCountForTesting = static_cast<int>(retryCount);
#endif
                    WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] retryCount=" << retryCount
                              << " -> continue [this = " << this << "]";
                    continue;
                }
#ifndef NDEBUG
                mGateRetryCountForTesting = static_cast<int>(retryCount);
#endif

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
                // Use a dedicated, shorter window here (fix #4b; the never-enforced UPLOADTIMEOUT
                // constant was retired in fu8 S8): a pure-handshake-failure loop
                // now surfaces onFail in <=60s instead of 180s. HANDSHAKEFAILTIMEOUT (60s) is the
                // default; runtime-overridable (ms) via MEGA_WS_HANDSHAKE_FAIL_WINDOW_MS through
                // Impl::handshakeFailWindowDs() -- kept coupled to the per-attempt handshake
                // timeout (MEGA_WS_HANDSHAKE_TIMEOUT_MS) so one attempt cannot exceed the window.
                dstime sustainedHandshakeFailureWindowDs = mImpl->handshakeFailWindowDs();
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                DEBUG_TEST_HOOK_WSUPLOAD_SUSTAINED_HANDSHAKE_FAILURE_WINDOW_DS(
                    sustainedHandshakeFailureWindowDs);
#endif
                if (retryCount >= 3 &&
                    failedForDs >= sustainedHandshakeFailureWindowDs)
                {
                    auto* uploadToFail = handshakeFailureCandidateLocked(nowDs);
#ifndef NDEBUG
                    ++mGateEvaluationCountForTesting;
#endif
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                    // C-7 seam: a test may veto the candidate — same branch as the real
                    // mid-migration null. fileno=0 signals the gate found none naturally.
                    {
                        bool vetoCandidate = false;
                        DEBUG_TEST_HOOK_WS_HANDSHAKE_FAILURE_CANDIDATE_VETO(
                            this,
                            uploadToFail ? uploadToFail->fileno() : 0,
                            vetoCandidate);
                        if (vetoCandidate)
                        {
                            uploadToFail = nullptr;
                        }
                    }
#endif
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
#ifndef NDEBUG
                        mGateRetryCountForTesting = 0;
                        mGateNullCandidateStreakForTesting = 0;
#endif
                        continue;
                    }
                    else
                    {
                        // No failable candidate right now (e.g. file mid-refresh/migration,
                        // or already unbound by a prior escalation). Do NOT let the window
                        // re-satisfy forever: reset it so we re-accumulate from scratch and
                        // re-evaluate on the next cycle once the candidate re-binds. This
                        // closes the pure-handshake-failure forever-loop (root_cause.md sec.4).
                        retryCount = 0;
                        firstConnectFailureDs = 0;
#ifndef NDEBUG
                        mGateRetryCountForTesting = 0;
                        ++mGateNullCandidateStreakForTesting;
#endif
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
                    // Capped-exponential backoff + jitter (fix #3). Replaces the flat
                    // CONNRETRYINTERVAL so N workers do not retry in lockstep and feed
                    // more handshakes into the convoy (root_cause.md S3c). retryCount is
                    // the worker-local consecutive-failure counter (maintained above);
                    // reconnectBackoffDsLocked reads only it (lock-free). On the clean
                    // link this branch is never taken (connectWS succeeds first), so it
                    // is a failure-only no-op. Composes with #4: this only changes the
                    // sleep duration, not retryCount/firstConnectFailureDs accounting.
                    const dstime backoffDs = reconnectBackoffDsLocked(retryCount);
                    ScopedUnlock unlock(lk);
                    // S12 Cluster-E fix: sliced, terminate-aware — an uninterruptible
                    // multi-second sleep here would defeat the bounded locallogout
                    // quiesce (wsLocallogoutCleanup) and read as a stuck worker.
                    for (dstime slept = 0; slept < backoffDs && !th->terminate; ++slept)
                    {
                        SteadyTime::sleep_ds(1);
                    }
                }
                continue;
            }
            retryCount = 0;
            firstConnectFailureDs = 0;
#ifndef NDEBUG
            mGateRetryCountForTesting = 0;
#endif
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
            // Goodput-saturation gate (SDK-5360, amendment A): a server-directed throttle pause
            // is NOT send-buffer backpressure. This continue happens BEFORE the haveSpace store
            // below, so clear the signal here or it would latch stale-true through the whole
            // pause and falsely feed the checkPools WIDEN predicate once the pause ends. Guarded
            // so gate=0 is byte-behaviour-identical.
            if (mImpl->mDatasetConnGate)
                ws->mBackpressured.store(false, std::memory_order_relaxed);
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

        // Goodput-saturation gate (SDK-5360, amendment A): publish send-buffer backpressure via
        // the per-conn atomic so WsPoolMgr::checkPools reads it cross-thread WITHOUT a racy raw
        // haveSpace() read (bufferedAmount/mDataLen are mutated in sendWS under ScopedUnlock).
        // Guarded so gate=0 leaves the flag false (byte-behaviour-identical). haveSpace() is
        // sampled once and reused by the existing branch below.
        const bool spaceAvail = ws->haveSpace();
        if (mImpl->mDatasetConnGate)
            ws->mBackpressured.store(!spaceAvail, std::memory_order_relaxed);
        if (!spaceAvail)
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
