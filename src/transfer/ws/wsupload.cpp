#ifdef MEGA_USE_WSUPLOAD

#include "mega/wsupload.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef> // offsetof
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// --- SDK headers (kept local to .cpp to keep the public header light)
#include "mega/command.h"
#include "mega/commands_ws.h"
#include "mega/file.h"
#include "mega/filesystem.h"
#include "mega/http.h"
#include "mega/logging.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "mega/testhooks.h"
#include "mega/transfer.h"
#include "mega/transfer/ws/ws_encryption.h"
#include "mega/transfer/ws/ws_pool_mgr.h"
#include "mega/types.h"
#include "mega/utils.h"

// File-internal types/helpers shared with future ws_curl.cpp (kMiB,
// WSUPLOAD_CURL_MULTI_POLL_MS, WSUPLOAD_TRACE, SteadyTime, ScopedUnlock,
// CRC32, WsBuf/WsChunk/ChunkHeader/ChunkFingerprintMacUpdate, WsConn,
// WsPoolThread, WsPool). Private header under src/, not include/. Relative
// include since src/ is not on the SDKlib include path.
#include "wsupload_internal.h"

// `class WsUploadFile` (promoted out of this TU in fu7-16 Goal 2.Step2) is
// pulled in here so that the WsConn / WsPool / ChunkFingerprintMacUpdate
// bodies below see the full definition, as do the inline `UploadEngine::Impl`
// bodies in `wsupload_engine.h`. Private header under src/, not include/.
#include "ws_upload_file.h"

// Full definition of `UploadEngine::Impl` (promoted out of this file in fu7-16
// Goal 2.Step0). `wsupload_engine.h` includes `ws_upload_file.h` so the
// header's inline `UploadEngine::Impl` member bodies see WsUploadFile complete.
#include "wsupload_engine.h"

// Phase 1: keep prototype's WS + threads. Phase 3: move into CurlHttpIO reactor.
#include <curl/curl.h>

#include <zlib.h>

namespace mega
{
namespace ws
{

// WSUPLOAD_TRACE, SteadyTime, ScopedUnlock, CRC32, kMiB, and
// WSUPLOAD_CURL_MULTI_POLL_MS now live in transfer/ws/wsupload_internal.h
// (included above). They are shared with the future ws_curl.cpp split.

#ifndef NDEBUG
std::uint64_t steadyMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
#endif

// WSUPLOAD_HANDSHAKE_TIMEOUT_MS and WSUPLOAD_HANDSHAKE_CV_POLL_MS now live in
// src/transfer/ws/ws_conn.cpp (anonymous namespace) next to their sole
// consumer WsConn::connectWS (fu7-16 Goal 2.Step3).

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

static ChunkMap g_chunkMap;

#ifndef NDEBUG
// Free function wrapper so unit tests can link against it via extern declaration.
int chunkSizeAtPosition(m_off_t pos)
{
    return g_chunkMap.chunksize(pos);
}
#endif

// WsPool, WsConn, WsPoolThread forward-declared in
// transfer/ws/wsupload_internal.h (included above). UploadEngine itself is
// declared by mega/wsupload.h.

// `enum class FailReason` now lives in src/transfer/ws/wsupload_internal.h
// (fu7-16 Goal 2.Step2) so WsUploadFile's inline `uploadFailed` body can
// resolve its enumerators.

// `enum class WsApiServerEvent` (server-event opcodes consumed by
// WsConn::onmessage) now lives in src/transfer/ws/ws_conn.cpp (anonymous
// namespace) alongside its sole consumer (fu7-16 Goal 2.Step3).

// WS payloads (ChunkHeader/WsChunk/WsBuf/ChunkFingerprintMacUpdate),
// WsConn, WsPoolThread and WsPool now live in
// transfer/ws/wsupload_internal.h. The WsConn / WsBuf::sendWS / WsPoolThread
// ctor / ChunkFingerprintMacUpdate::apply bodies were split out to
// src/transfer/ws/ws_conn.cpp in fu7-16 Goal 2.Step3; WsPool::* bodies remain
// in this translation unit pending the Goal 2.Step4 split.

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
#endif


// ---------- Pool manager (USC refresh + cURL multi) ----------
// struct WsPoolMgr is declared in include/mega/transfer/ws/ws_pool_mgr.h.
// Its method bodies live in src/transfer/ws/ws_pool_mgr.cpp (fu7-16
// Goal 2.Step1) and src/transfer/ws/ws_curl.cpp (fu7-14 G2.a-β).

// ========== UploadEngine::Impl ==========
// Definition lives in src/transfer/ws/wsupload_engine.h (promoted out of this
// TU in fu7-16 Goal 2.Step0); the header is included near the top of this
// file. WsUploadFile (referenced from Impl's inline member bodies) lives in
// src/transfer/ws/ws_upload_file.h (promoted out of this TU in fu7-16
// Goal 2.Step2); ws_upload_file.h is included by wsupload_engine.h.

// ========== WsConn ==========
// All WsConn member bodies (ctor / dtor / connectWS / closeWS / onopen /
// onclose / resetBufferedSendState / curlSend / curlRecv / failFileLocked /
// handleBytesConfirmedOverflow / onmessage / sendChunkData), plus the free
// `WsBuf::sendWS`, `WsPoolThread` ctor and `ChunkFingerprintMacUpdate::apply`,
// live in src/transfer/ws/ws_conn.cpp (fu7-16 Goal 2.Step3). The WS upload
// handshake timeout constants and the TU-local WsApiServerEvent enum moved
// with them.

#ifndef NDEBUG
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

    const bool hasEligibleFile = impl.hasEligibleFileForPoolForTesting(mMinFileSize,
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
#endif

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

                const int advance = g_chunkMap.chunksize(chunk.pos);
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
    auto ws = std::make_unique<WsConn>(this);

    WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] BEGIN [lastQueueVersion=" << lastQueueVersion
              << "] [this = " << this << "]";

    std::unique_lock<std::mutex> lk(mImpl->uploadMutex);
    while (!th->terminate)
    {
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

    if (ws->readyState.load(std::memory_order_relaxed) != WsConn::ReadyState::CLOSED)
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

// ========== WsPoolMgr (Impl/WsUploadFile-coupled remainder) ==========
// Most WsPoolMgr method bodies live in sibling translation units:
//  - src/transfer/ws/ws_pool_mgr.cpp (fu7-16 Goal 2.Step1): ctor/dtor,
//    bumpLastNetRead, bumpAllPools, markPoolRetiring, poolHasNoWork,
//    cleanupRetiringPools, applyRefreshBackoff.
//  - src/transfer/ws/ws_curl.cpp (fu7-14 G2.a-β): curlIO, ensurePinnedPool.
//
// The bodies kept here all need UploadEngine::Impl members (which transitively
// require WsUploadFile complete via wsupload_engine.h's inline accessors)
// and/or WsUploadFile members directly. WsUploadFile is TU-local to this TU
// until fu7-16 Goal 2.Step2 promotes its declaration to a header.
// The header declaration for WsPoolMgr is in
// include/mega/transfer/ws/ws_pool_mgr.h.
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

// Forwarding helper for ws_curl.cpp. Lives here because UploadEngine::Impl is
// only fully usable via wsupload_engine.h once WsUploadFile is in scope; this
// TU is where WsUploadFile is defined, so the include resolves cleanly.
unsigned char WsPoolMgr::pinnedPoolConnectionLimit() const
{
    return mImpl->poolConnectionLimit();
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

// fu7-15 G2.a-3: forwarders always-compile; see wsupload.h note above.
bool UploadEngine::getPoolStateForTesting(const std::string& url,
                                          UploadEngine::PoolStateForTesting& out) const
{
    return pImpl->getPoolStateForTesting(url, out);
}

bool UploadEngine::getWsUploadStatsForTesting(WsUploadStatsForTesting& out) const
{
    return pImpl->getWsUploadStatsForTesting(out);
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
