/**
 * @file src/transfer/ws/ws_conn.cpp
 * @brief Non-trivial method bodies for the WebSocket-upload connection layer,
 *        split out of `src/transfer/ws/wsupload.cpp`. Hosts:
 *
 *          - `WsConn` ctor/dtor + connectWS/closeWS/onopen/onclose lifecycle.
 *          - `WsConn::curlSend` / `curlRecv` cURL drain loops.
 *          - `WsConn::onmessage` — the per-frame server-event dispatch (the
 *            deepest Impl/WsUploadFile/WsPoolMgr coupling point in the engine).
 *          - `WsConn::failFileLocked` / `handleBytesConfirmedOverflow` /
 *            `sendChunkData` helpers.
 *          - `WsBuf::sendWS` (free-standing buffer drain).
 *          - `WsPoolThread` ctor (needs the full `WsPool::poolWorkerThread`
 *            address, so it could not be inline in the internal header).
 *          - `ChunkFingerprintMacUpdate::apply` (out-of-line because it calls
 *            `WsUploadFile::queueConfirmedChunkMacs`).
 *
 *        Relies on `mega/transfer/ws/wsupload_internal.h` for the `WsConn` /
 *        `WsBuf` / `WsPoolThread` / `ChunkFingerprintMacUpdate` / `WsPool`
 *        cluster, `mega/transfer/ws/wsupload_engine.h` for the full
 *        `UploadEngine::Impl` definition (every `mPool->mImpl->X` deref in
 *        `onmessage` requires it), and `mega/transfer/ws/ws_upload_file.h`
 *        for the `WsUploadFile` complete type touched by `findFile`,
 *        `onmessage`, `failFileLocked`, `handleBytesConfirmedOverflow`, and
 *        `ChunkFingerprintMacUpdate::apply`.
 *
 *        The TU-local `enum class WsApiServerEvent` (server-event opcodes)
 *        moves with `WsConn::onmessage` since that is its sole consumer.
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

// `mega/megaapp.h` must precede `mega/transfer/ws/wsupload_engine.h` so that
// the inline `UploadEngine::Impl::onTransferAdded`-style member bodies in the
// engine header see `MegaApp` as a complete type (they dereference
// `client.app`). Also pulled in by wsupload.cpp for the same reason.
#include "mega/megaapp.h"

// File-internal types shared with wsupload.cpp (WsConn, WsBuf, WsPoolThread,
// ChunkFingerprintMacUpdate, WsPool, SteadyTime, WSUPLOAD_TRACE, FailReason,
// CRC32, ChunkHeader, WsChunk). SDK-internal architecture header.
#include "mega/transfer/ws/wsupload_internal.h"

// Full definition of `UploadEngine::Impl`. Required because every
// `mPool->mImpl->X` dereference in WsConn::onmessage / connectWS /
// failFileLocked needs Impl to be a complete type. The header transitively
// includes `mega/transfer/ws/ws_upload_file.h` so `WsUploadFile` is complete
// too.
#include "mega/transfer/ws/wsupload_engine.h"

// `class WsUploadFile` is also pulled in directly: `findFile` returns
// `WsUploadFile*`, and `failFileLocked` / `handleBytesConfirmedOverflow` /
// `onmessage` invoke its inline accessors. Already pulled in via
// wsupload_engine.h, but re-state for clarity (mirrors the pattern in
// ws_upload_file.cpp / ws_curl.cpp).
#include "mega/transfer/ws/ws_upload_file.h"

#include "mega/logging.h"
#include "mega/megaclient.h" // MegaClient::wsPostToClientThread / wsHandshakeForUpload / wsEngine
#include "mega/testhooks.h" // DEBUG_TEST_HOOK_WSUPLOAD_SERVER_EVENT / CORRUPT_TOKEN, WsUploadServerEventAction, globalMegaTestHooks
#include "mega/transfer/ws/ws_pool_mgr.h" // WsPoolMgr::bumpLastNetRead / refreshPools / mActiveFiles
#include "mega/wsupload.h" // UploadEngine::FailureDisposition, InboundFrameValidationResult, detail::validateInboundFrame, kInboundChunkResponseBytes, kInboundFrameTrailerCrcBytes

#include <chrono>
#include <condition_variable>
#include <cstddef> // offsetof
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <curl/curl.h>

namespace mega
{
namespace ws
{

namespace
{

// The per-attempt WS TLS+upgrade handshake timeouts (baseline 15s / loss-adaptive 45s, with the
// rationale for each) moved to UploadEngine::Impl (wsupload_engine.h) as kHandshakeTimeoutMs /
// kHandshakeTimeoutLossMs so the MEGA_WS_HANDSHAKE_TIMEOUT_MS runtime override can select
// through one accessor (Impl::handshakeTimeoutMs). Consumed below in WsConn::connectWS.

// Poll interval for the WsConn::connectWS handshake-completion condition variable.
// Short enough to react to stopping() in <1s; large enough to avoid spinning while
// the curl handshake makes progress on the client thread.
constexpr int WSUPLOAD_HANDSHAKE_CV_POLL_MS = 200;

// The worker baton must OUTLIVE the handshake's own CURLOPT_TIMEOUT so a slow
// handshake is terminated by curl (fast, definitive, frees the easy) rather than
// abandoned by the baton (which leaves the easy running on the client thread).
// Slack covers FIFO queueing + the 200ms CV poll granularity. Added to whichever
// handshake timeout (baseline or loss) is in effect for this attempt.
constexpr int WSUPLOAD_HANDSHAKE_BATON_SLACK_MS = 5000;

// TU-local enum: server-event opcodes for the WS upload response stream.
// Only used by WsConn::onmessage; moved together with the body.
enum class WsApiServerEvent: signed char
{
    ChunkIngested = 1, // chunk ingested (non-final)
    AlreadyOnServer = 2, // already on server (after reconnect)
    CrcFailed = 3, // chunk CRC failed
    UploadCompleted = 4, // upload completed
    Distress = 5, // server requested pool refresh
    Throttle = 6, // server requested temporary pause
    FinalDataIngested = 7 // final data ingested (server knows file is complete)
};

} // namespace

// ========== WsConn ==========
WsConn::WsConn(WsPool* pool):
    mPool(pool)
{
    assert(pool);
    assert(pool->mImpl);

    // Protect pool->mConns against concurrent access (e.g. UploadEngine::Impl::remove() iterating it)
    std::lock_guard<std::mutex> lk(pool->mImpl->uploadMutex);
    mConns_it = pool->mConns.insert(this).first;
}

WsConn::~WsConn()
{
    if (mPool && mPool->mImpl)
    {
        std::lock_guard<std::mutex> lk(mPool->mImpl->uploadMutex);
        // Any in-flight chunk should have been requeued or closed out by closeWS() before
        // the owning pool-worker thread exits and destroys the WsConn. The assert fires
        // loudly in Debug; C3 insurance (fix #6): in Release the dead assert is a no-op,
        // so without this requeue the chunks would be silently dropped and
        // mNumChunksInFlight left stale-high (pool wedge). Mirror
        // retryChunksOnTheWireLocked under the same already-held uploadMutex.
        assert(mChunksInFlight.empty() &&
               "WsConn destroyed with in-flight chunks; closeWS() must run first");
        if (!mChunksInFlight.empty())
        {
            for (auto& p: mChunksInFlight)
            {
                mPool->mToResend.push_back(p.first);
            }
            mPool->mNumChunksInFlight -= static_cast<int>(mChunksInFlight.size());
            mChunksInFlight.clear();
        }
        mPool->mConns.erase(mConns_it);
    }
    if (curl)
    {
        curl_easy_cleanup(curl);
    }
}

bool WsConn::connectWS()
{
    WSUPLOAD_TRACE << "[WsConn::connectWS] BEGIN [this = " << this << "]";
    if (mPool->mImpl->stopping())
    {
        readyState.store(ReadyState::CLOSED, std::memory_order_relaxed);
        return false;
    }
    if (curl)
    {
        WSUPLOAD_TRACE << "[WsConn::connectWS] curl already exists, cleanup [this = " << this << "]";
        // Fix #6: a handle that carried a half-sent frame must be fully cleaned up, never
        // reused, so its mid-frame state cannot start a fresh frame out of sync. closeWS()
        // must have run first (which clears mFrameInProgress via resetBufferedSendState).
        assert((!mBufs[0].mFrameInProgress && !mBufs[1].mFrameInProgress) &&
               "connectWS() reached with a partial frame staged; closeWS() must run first");
        curl_easy_cleanup(curl);
        curl = nullptr;
    }
    readyState.store(ReadyState::CONNECTING, std::memory_order_relaxed);

    // SDK-5360 Design A (mParallelHandshake, default ON; ANDed with mLossRecovery): run the
    // TLS+WS-upgrade handshake DIRECTLY on this worker thread instead of bouncing it to the
    // client thread via a Baton + wsPostToClientThread + watchdog. N pool workers then
    // handshake concurrently, so connections 5-8 open in time to carry a small file's chunks
    // (vs the serial ~46s under loss that develop avoids). uploadMutex is already released
    // around connectWS() (ScopedUnlock at the caller), so this blocking call never holds the
    // engine lock. Safe off the client thread because: (a) wsHandshakeForUpload is a thin
    // wrapper over CurlHttpIO::wsHandshake; httpio is set-once/read-only; (b) curlsh now
    // carries lock callbacks (net.cpp) for the concurrent DNS/SSL-session cache writes; and
    // (c) the read-mostly client state wsHandshake reads (dnsservers/proxy*/useragent) is set
    // at init and not mutated during uploads, so concurrent reads are benign. CURLOPT_TIMEOUT_MS
    // inside wsHandshake bounds the blocking call, so no separate baton watchdog is needed.
    if (mPool->mImpl->mParallelHandshake)
    {
        const std::string url = mPool->mUrl;

        // T1b: loss-adaptive handshake timeout. mAdaptiveHandshake is read-once at engine
        // construction and never mutated, so reading it here is benign. handshakeTimeoutMs()
        // also applies the MEGA_WS_HANDSHAKE_TIMEOUT_MS override (S7 Lever A) when set.
        const long handshakeTimeoutMs =
            mPool->mImpl->handshakeTimeoutMs(mPool->mImpl->mAdaptiveHandshake);

        if (auto* engine = mPool->mImpl->client.wsEngine(); !engine || engine->isStopping())
        {
            readyState.store(ReadyState::CLOSED, std::memory_order_relaxed);
            return false;
        }
        if (mPool->mImpl->stopping())
        {
            readyState.store(ReadyState::CLOSED, std::memory_order_relaxed);
            return false;
        }

        std::string err;
        CURL* e = static_cast<CURL*>(
            mPool->mImpl->client.wsHandshakeForUpload(url, handshakeTimeoutMs, &err));
        if (!e)
        {
            WSUPLOAD_TRACE << "[WsConn::connectWS] (parallel) wsHandshakeForUpload -> nullptr"
                           << (err.empty() ? "" : (", err=" + err))
                           << " -> readyState=CLOSED, return false [this = " << this << "]";
            readyState.store(ReadyState::CLOSED, std::memory_order_relaxed);
            return false;
        }

        curl = e; // this worker thread exclusively owns the handle now
        readyState.store(ReadyState::OPEN, std::memory_order_relaxed);
        // Fix #6: a fresh handle must never inherit a stale partial frame; closeWS() runs
        // before any reconnect and resets the send cursor + mFrameInProgress.
        assert(!mBufs[0].mFrameInProgress && !mBufs[1].mFrameInProgress &&
               mBufs[0].mSendPos == 0 && mBufs[1].mSendPos == 0 &&
               "WsConn reconnected with a stale partial frame; closeWS() must run first");
        onopen(); // your existing callback
        WSUPLOAD_TRACE << "[WsConn::connectWS] (parallel) END -> success, return true [this = "
                       << this << "]";
        return true;
    }

    struct Baton
    {
        std::mutex m;
        std::condition_variable cv;
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy{nullptr, &curl_easy_cleanup};
        bool done = false;
    };

    auto baton = std::make_shared<Baton>();
    void* self = this;

    const std::string url = mPool->mUrl;

    // T1b: loss-adaptive handshake timeout. mAdaptiveHandshake is read-once at engine
    // construction and never mutated, so reading it here (with uploadMutex released
    // around the blocking handshake) is benign. When the flag is on we grant the loss-adaptive
    // timeout instead of the baseline 15s; handshakeTimeoutMs() also applies the
    // MEGA_WS_HANDSHAKE_TIMEOUT_MS override (S7 Lever A) when set.
    const long handshakeTimeoutMs =
        mPool->mImpl->handshakeTimeoutMs(mPool->mImpl->mAdaptiveHandshake);
    const int handshakeBatonMs =
        static_cast<int>(handshakeTimeoutMs) + WSUPLOAD_HANDSHAKE_BATON_SLACK_MS;

    mPool->mImpl->client.wsPostToClientThread(
        [baton, url, self, handshakeTimeoutMs](MegaClient& client, TransferDbCommitter&)
        {
            WSUPLOAD_TRACE << "[WsConn::connectWS] [client.wsPostToClientThread] BEGIN [this = " << self
                      << "]";
            if (auto* engine = client.wsEngine(); !engine || engine->isStopping())
            {
                std::lock_guard<std::mutex> g(baton->m);
                baton->easy.reset();
                baton->done = true;
                baton->cv.notify_one();
                return;
            }
            std::string err;
            CURL* e = static_cast<CURL*>(
                client.wsHandshakeForUpload(url, handshakeTimeoutMs, &err));

            {
                std::lock_guard<std::mutex> g(baton->m);
                baton->easy.reset(e);
                baton->done = true;
                if (e)
                {
                    if (!err.empty())
                    {
                        WSUPLOAD_TRACE << "[WsConn::connectWS] [client.wsPostToClientThread] "
                                     "wsHandshakeForUpload failed, err="
                                  << err << ", set baton.easy=" << (void*)baton->easy.get()
                                  << " [this = " << self
                                  << "]";
                    }
                    else
                    {
                        WSUPLOAD_TRACE << "[WsConn::connectWS] [client.wsPostToClientThread] "
                                     "wsHandshakeForUpload success, set baton.easy="
                                  << (void*)baton->easy.get() << " [this = " << self << "]";
                    }
                }
                else
                {
                    WSUPLOAD_TRACE << "[WsConn::connectWS] [client.wsPostToClientThread] "
                                 "wsHandshakeForUpload returned nullptr, set baton.easy=nullptr [this = "
                              << self << "]";
                }
            }
            baton->cv.notify_one();
            WSUPLOAD_TRACE << "[WsConn::connectWS] [client.wsPostToClientThread] END [this = " << self
                      << "]";
        });

    // Wait here on the worker thread until one of the following happens:
    //  1) handshake completes (success or failure including handshake timeout);
    //  2) WS engine stop is requested (stopping());
    //  3) local 20s watchdog deadline is reached.
    std::unique_lock<std::mutex> lk(baton->m);
    const auto waitDeadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(handshakeBatonMs);
    while (!baton->done)
    {
        if (mPool->mImpl->stopping())
        {
            readyState.store(ReadyState::CLOSED, std::memory_order_relaxed);
            return false;
        }

        if (std::chrono::steady_clock::now() >= waitDeadline)
        {
            WSUPLOAD_TRACE << "[WsConn::connectWS] handshake baton timed out -> readyState=CLOSED and "
                         "return false [this = "
                      << this << "]";
            readyState.store(ReadyState::CLOSED, std::memory_order_relaxed);
            return false;
        }

        baton->cv.wait_for(lk, std::chrono::milliseconds(WSUPLOAD_HANDSHAKE_CV_POLL_MS), [&]
                           {
                               return baton->done;
                           });
    }

    if (!baton->easy)
    {
        WSUPLOAD_TRACE
            << "[WsConn::connectWS] !baton.easy -> readyState=CLOSED and return false [this = "
            << this << "]";
        readyState.store(ReadyState::CLOSED, std::memory_order_relaxed);
        return false;
    }

    curl = baton->easy.release(); // worker thread exclusively owns the handle now
    readyState.store(ReadyState::OPEN, std::memory_order_relaxed);
    // Fix #6: a fresh handle must never inherit a stale partial frame; closeWS() runs
    // before any reconnect and resets the send cursor + mFrameInProgress.
    assert(!mBufs[0].mFrameInProgress && !mBufs[1].mFrameInProgress &&
           mBufs[0].mSendPos == 0 && mBufs[1].mSendPos == 0 &&
           "WsConn reconnected with a stale partial frame; closeWS() must run first");
    onopen(); // your existing callback
    WSUPLOAD_TRACE << "[WsConn::connectWS] END -> success, return true [this = " << this << "]";
    return true;
}

void WsConn::closeWS()
{
    WSUPLOAD_TRACE << "[WsConn::closeWS] BEGIN [this = " << this << "]";
    if (readyState.load(std::memory_order_relaxed) == ReadyState::CLOSED)
    {
        WSUPLOAD_TRACE << "[WsConn::closeWS] readyState=CLOSED, return [this = " << this << "]";
        return;
    }
    // Clear the deferred-close flag: once closeWS actually runs, any pending
    // request has been honoured. Worker thread is single-owner, so this is
    // a belt-and-suspenders write documenting intent.
    mPendingClose = false;
    readyState.store(ReadyState::CLOSED, std::memory_order_relaxed);
    // Fix #6 teardown hygiene: if a buffer holds a partially-sent WS frame, the libcurl
    // easy (about to be curl_easy_cleanup-ed on reconnect) owns half-frame state that
    // cannot continue on a fresh handle. We discard the SDK cursor (resetBufferedSendState
    // below clears mSendPos + mFrameInProgress) and rely on onclose()->
    // retryChunksOnTheWire() to requeue the whole chunk for a fresh send.
    const bool hadPartialFrame = mBufs[0].mFrameInProgress || mBufs[1].mFrameInProgress;
    (void)hadPartialFrame;
#ifndef NDEBUG
    if (hadPartialFrame)
    {
        ++mPartialFrameTornDownCount;
    }
#endif
    resetBufferedSendState();
    onclose();
    WSUPLOAD_TRACE << "[WsConn::closeWS] END [this = " << this << "]";
}

void WsConn::onopen()
{
    // Ack-stall watchdog baseline (fu8 S6): seed the per-conn server-liveness stamp at
    // open so a freshly-connected conn is not instantly flagged as stalled before its
    // first ack. Runs with uploadMutex released (ScopedUnlock around connectWS), hence
    // the atomic store; a fresh SteadyTime::ds() read keeps it on the same monotonic
    // clock as WsPoolMgr::checkPools' impl.currentTime comparison.
    mLastInboundFrameDs.store(SteadyTime::ds(), std::memory_order_relaxed);
    mLastSendProgressDs.store(SteadyTime::ds(), std::memory_order_relaxed);
    WSUPLOAD_TRACE << "[WsConn::onopen] Connected to " << mPool->mUrl;
}

void WsConn::onclose()
{
    WSUPLOAD_TRACE << "[WsConn::onclose] BEGIN [Disconnected from " << mPool->mUrl
              << "] [this = " << this << "]";
    if (mPool)
    {
        WSUPLOAD_TRACE << "[WsConn::onclose] mPool->retryChunksOnTheWire(this) [this = " << this << "]";
        mPool->retryChunksOnTheWire(this);
    }
    WSUPLOAD_TRACE << "[WsConn::onclose] END [this = " << this << "]";
}

void WsConn::resetBufferedSendState() noexcept
{
    mBufs[0].reset();
    mBufs[1].reset();
    mCurBuf = 0;
    mInPos = 0;
    bufferedAmount = 0;
    // Goodput-saturation gate (SDK-5360): a closed conn holds no send-buffer state, so it is
    // not backpressured. resetBufferedSendState() is the sole close path (called from
    // closeWS() right after readyState := CLOSED), so clearing here keeps the checkPools WIDEN
    // predicate from reading a stale-true on a conn that has torn down / is about to
    // re-handshake. Unconditional (not gate-guarded): inert when the gate is off because the
    // worker never SETs the flag true then, so this is a harmless false->false store.
    mBackpressured.store(false, std::memory_order_relaxed);
}

void WsConn::curlSend()
{
    WSUPLOAD_TRACE << "[WsConn::curlSend] BEGIN [readyState="
              << static_cast<int>(readyState.load(std::memory_order_relaxed))
              << "] [this = " << this << "]";
    if (readyState.load(std::memory_order_relaxed) != ReadyState::OPEN)
    {
        WSUPLOAD_TRACE << "[WsConn::curlSend] readyState != ReadyState::OPEN, return [this = " << this
                  << "]";
        return;
    }
    while (mBufs[static_cast<unsigned char>(mCurBuf)].sendWS(this, bufferedAmount))
        mCurBuf = !mCurBuf;
    WSUPLOAD_TRACE << "[WsConn::curlSend] END [this = " << this << "]";
}

void WsConn::curlRecv()
{
    WSUPLOAD_TRACE << "[WsConn::curlRecv] BEGIN [readyState="
              << static_cast<int>(readyState.load(std::memory_order_relaxed))
              << "] [this = " << this << "]";
    if (readyState.load(std::memory_order_relaxed) != ReadyState::OPEN)
    {
        WSUPLOAD_TRACE << "[WsConn::curlRecv] readyState != ReadyState::OPEN, return [this = " << this
                  << "]";
        return;
    }

    const struct curl_ws_frame* meta = nullptr;
    size_t recv = 0;
    unsigned drainedAfterPendingClose = 0;

    for (;;)
    {
        WSUPLOAD_TRACE << "[WsConn::curlRecv] curl_ws_recv(curl, mInBuf(=" << (void*)mInBuf
                  << "), sizeof(mInBuf)(=" << sizeof(mInBuf) << "), &recv, &meta) [this = " << this
                  << "]";
        const CURLcode res = curl_ws_recv(curl, mInBuf, sizeof(mInBuf), &recv, &meta);
        if (res == CURLE_OK && meta && !meta->bytesleft && recv > 0)
        {
            WSUPLOAD_TRACE << "[WsConn::curlRecv] res == CURLE_OK && meta && !meta->bytesleft && recv(="
                      << recv
                      << ") > 0 -> onmessage(mInBuf, static_cast<int>(recv)) [this = " << this
                      << "]";
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
            // fu8 S6 ack-stall repro: discard this inbound frame WITHOUT running
            // onmessage(), so the per-conn liveness stamp (mLastInboundFrameDs) is never
            // refreshed while the socket stays OPEN and sends continue (silent-but-OPEN
            // server). Loop back to drain+discard any further frames; CURLE_AGAIN breaks
            // below. Non-test builds: the macro is empty, swallowFrame stays false.
            {
                bool swallowFrame = false;
                DEBUG_TEST_HOOK_WS_RECV_SWALLOW(swallowFrame);
                if (swallowFrame)
                {
                    WSUPLOAD_TRACE << "[WsConn::curlRecv] WS_RECV_SWALLOW discard inbound frame (recv="
                              << recv << ") [this = " << this << "]";
                    continue;
                }
            }
#endif
            onmessage(mInBuf, static_cast<int>(recv));
            // If the prior onmessage requested a deferred close, keep draining
            // libcurl's recv pipeline. The curl handle remains valid because
            // closeWS() does not invoke curl_easy_cleanup (that only happens in
            // ~WsConn / the reconnect path). Break only when the state has
            // transitioned to non-OPEN for any reason OTHER than our own
            // pending-close request.
            if (readyState.load(std::memory_order_relaxed) != ReadyState::OPEN && !mPendingClose)
            {
                WSUPLOAD_TRACE << "[WsConn::curlRecv] onmessage closed connection -> break [this = "
                          << this << "]";
                break;
            }
            if (mPendingClose)
                ++drainedAfterPendingClose;
        }
        else
        {
            if (res != CURLE_AGAIN || meta)
            {
                WSUPLOAD_TRACE << "[WsConn::curlRecv] res(=" << res
                          << ") != CURLE_AGAIN || meta -> closeWS() [this = " << this << "]";
                closeWS();
            }
#ifndef NDEBUG
            else
            {
                // Count CURLE_AGAIN on recv (per-conn).
                ++mCurlAgainRecvCount;
            }
#endif
            WSUPLOAD_TRACE << "[WsConn::curlRecv] res(=" << res
                      << ") != CURLE_OK || !meta || meta->bytesleft(="
                      << (meta ? meta->bytesleft : 0) << ") > 0 || recv(=" << recv
                      << ") <= 0 -> break [this = " << this << "]";
            break;
        }
    }

    // Honour any deferred-close request now that the recv pipeline has been
    // drained. closeWS() is idempotent (guards on readyState==CLOSED), so a
    // second call from the CURLE_AGAIN-else branch above harmlessly no-ops.
    if (mPendingClose)
    {
        WSUPLOAD_TRACE << "[WsConn::curlRecv] drained " << drainedAfterPendingClose
                  << " frames before closeWS [this = " << this << "]";
        closeWS();
    }
    WSUPLOAD_TRACE << "[WsConn::curlRecv] END [this = " << this << "]";
}

void WsConn::failFileLocked(WsUploadFile* uf,
                            const std::uint32_t fileno,
                            const FailReason reason,
                            const int apierr,
                            const m_off_t aux,
                            const UploadEngine::FailureDisposition disp)
{
    mPool->purgeFileLocked(fileno);
    uf->uploadFailed(reason);
    if (mPool->mImpl->mCb.onFail)
    {
        mPool->mImpl->mCb.onFail(uf->transfer(), apierr, aux, disp);
    }
}

bool WsConn::handleBytesConfirmedOverflow(WsUploadFile* uf)
{
    if (uf->bytesConfirmed() <= uf->size())
        return false;

    LOG_warn << "[WsConn::onmessage] uf->bytesConfirmed(=" << uf->bytesConfirmed()
             << ") > uf->size(=" << uf->size()
             << ") -> server confirmed beyond expected size, failing upload [this = "
             << this << "]";
    failFileLocked(uf,
                   uf->fileno(),
                   FailReason::StateLost,
                   API_EINTERNAL,
                   uf->bytesConfirmed(),
                   UploadEngine::FailureDisposition::Retryable);
    return true;
}

void WsConn::onmessage(const char* msg, const int len)
{
    WSUPLOAD_TRACE << "[WsConn::onmessage] BEGIN [len=" << len << "] [this = " << this << "]";
    switch (detail::validateInboundFrame(msg, len))
    {
        case detail::InboundFrameValidationResult::TooShort:
            LOG_warn << "WsUpload: invalid server msg len=" << len;
            mPendingClose = true;
            return;

        case detail::InboundFrameValidationResult::BadCrc:
            LOG_warn << "WsUpload: inbound CRC failed, byteLength=" << len;
            mPendingClose = true;
            return;

        case detail::InboundFrameValidationResult::Ok:
            break;
    }

    // From here on we must protect pool/file/transfer state with the upload mutex.
    std::lock_guard<std::mutex> lk(mPool->mImpl->uploadMutex);

    mPool->mLastActive = mPool->mImpl->currentTime;
    mPool->mLastServerResponse = mPool->mImpl->currentTime;
    // Ack-stall watchdog (fu8 S6): per-conn server-liveness stamp. Refreshed on every
    // validated inbound server frame so checkPools only force-reconnects a conn whose
    // server acks have genuinely gone silent past ACKSTALLTIMEOUT.
    mLastInboundFrameDs.store(mPool->mImpl->currentTime, std::memory_order_relaxed);
    mPool->mImpl->poolMgr.bumpLastNetRead(mPool->mImpl->currentTime);

#pragma pack(push, 1)

struct ChunkResponse
{
    std::uint32_t fileno;
    m_off_t chunkpos;
    signed char event;
};

#pragma pack(pop)

    // WS completion frame layout: [ChunkResponse][payloadLength:1][payload:N][crc32:4]
    constexpr int kWsChunkResponseHeaderSize = sizeof(ChunkResponse);
    static_assert(kWsChunkResponseHeaderSize ==
                    static_cast<std::size_t>(detail::kInboundChunkResponseBytes),
                "ChunkResponse doesn't match detail::kInboundChunkResponseBytes");

    const auto* response = reinterpret_cast<const ChunkResponse*>(msg);
    m_off_t chunkPos = response->chunkpos;
    auto event = static_cast<WsApiServerEvent>(response->event);
    WSUPLOAD_TRACE << "[WsConn::onmessage] response->fileno=" << response->fileno
              << " response->chunkpos=" << response->chunkpos
              << " response->event=" << static_cast<int>(event) << " [this = " << this << "]";
    WsChunk chunk;

    WsUploadFile* uf = mPool->findFile(response->fileno, *mPool->mImpl);
    if (!uf)
    {
        WSUPLOAD_TRACE << "[WsConn::onmessage] !uf -> return [this = " << this << "]";
        return; // file cancelled or moved
    }

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    int hookEvent = static_cast<int>(event);
    WsUploadServerEventAction hookAction = WsUploadServerEventAction::None;
    DEBUG_TEST_HOOK_WSUPLOAD_SERVER_EVENT(response->fileno, hookEvent, chunkPos, hookAction);
    if (hookAction == WsUploadServerEventAction::Drop)
    {
        LOG_warn << "WsUpload: debug hook dropped server event=" << static_cast<int>(event)
                 << " fileno=" << response->fileno;
        return;
    }
    if (hookAction == WsUploadServerEventAction::Modify)
    {
        LOG_warn << "WsUpload: debug hook modified server event=" << static_cast<int>(event)
                 << " -> " << hookEvent << " chunkpos=" << response->chunkpos << " -> " << chunkPos
                 << " fileno=" << response->fileno;
        event = static_cast<WsApiServerEvent>(hookEvent);
    }
#endif

    if (event < WsApiServerEvent::UploadCompleted || event == WsApiServerEvent::FinalDataIngested)
    {
        if (static_cast<int>(event) < 0)
        {
            WSUPLOAD_TRACE << "[WsConn::onmessage] response->event < 0 -> "
                         "uf->uploadFailed(FailReason::ServerError) [this = "
                      << this << "]";
            // A server-side error aborts the current upload attempt.
            // Purge in-flight/resend state for this file before unbinding it from the pool.
            failFileLocked(uf,
                           response->fileno,
                           FailReason::ServerError,
                           static_cast<int>(event),
                           chunkPos,
                           UploadEngine::FailureDisposition::Retryable);
            return;
        }

        WSUPLOAD_TRACE << "[WsConn::onmessage] response->event >= 0 -> chunk.pos = -1 [this = " << this
                  << "]";
        chunk.pos = -1;
        const bool shouldApply = event == WsApiServerEvent::ChunkIngested ||
                                 event == WsApiServerEvent::AlreadyOnServer ||
                                 event == WsApiServerEvent::FinalDataIngested;
        for (auto it = mChunksInFlight.begin(); it != mChunksInFlight.end(); ++it)
        {
            if (it->first.pos == chunkPos && it->first.fileno == response->fileno)
            {
                chunk = it->first;
                if (shouldApply)
                    it->second.apply(chunk.pos, *uf);
                mChunksInFlight.erase(it);
                if (mPool->mNumChunksInFlight > 0)
                    --mPool->mNumChunksInFlight;
                break;
            }
        }
        if (chunk.pos < 0)
        {
            LOG_warn << "WsUpload: PROTOCOL - acked chunk not in-flight [pos=" << chunkPos
                     << " fileno=" << response->fileno << " type=" << static_cast<int>(event)
                     << "]";
            return;
        }
    }

    if (len == kWsChunkResponseHeaderSize)
    {
        WSUPLOAD_TRACE << "[WsConn::onmessage] len == kWsChunkResponseHeaderSize -> "
                     "uf->uploadFailed(FailReason::Unknown) [this = "
                  << this << "]";
        // Unknown/invalid server response for this upload attempt.
        failFileLocked(uf,
                       response->fileno,
                       FailReason::Unknown,
                       API_EAGAIN,
                       0,
                       UploadEngine::FailureDisposition::Retryable);
        return;
    }

    switch (event)
    {
        case WsApiServerEvent::ChunkIngested: // non-final
#ifndef NDEBUG
            // Throttle-recovery latency = pause-start to first chunk-ack on this conn.
            if (mPauseStartedAtMs)
            {
                const auto deltaDs = static_cast<dstime>(SteadyTime::ds()) - mPauseStartedAtMs;
                const auto deltaMs = static_cast<std::uint64_t>(dsToMs(deltaDs));
                ++mThrottleRecoveryAckSamples;
                mThrottleRecoveryAckTotalMs += deltaMs;
                if (deltaMs > mThrottleRecoveryAckMaxMs) mThrottleRecoveryAckMaxMs = deltaMs;
                mPauseStartedAtMs = 0;
            }
#endif
            WSUPLOAD_TRACE
                << "[WsConn::onmessage] response->event == 1 chunk ingested (non-final) [chunk.len="
                << chunk.len << "] [this = " << this << "]";
            if (chunk.len)
            {
                uf->onServerConfirmedBytes(chunk.len);
                // Goodput-saturation gate (SDK-5360): aggregate this pool's server-confirmed
                // bytes so the checkPools ramp controller can measure goodput gain. Under
                // uploadMutex (held from :609) so a plain add is race-free (HR23). Guarded on
                // the gate so MEGA_WS_DATASET_CONN_GATE=0 is byte-behaviour-identical.
                if (mPool->mImpl->mDatasetConnGate)
                    mPool->mConfirmedBytesTotal += static_cast<std::uint64_t>(chunk.len);
                // Tier 2 A (loss-recovery): record this whole-chunk range as acked so a
                // later reconnect re-queues only un-acked chunks (gated; clean no-op).
                if (mPool->mImpl->mAckedChunkRewind)
                    uf->markRangeAcked(chunk.pos, chunk.pos + chunk.len);
                if (mPool->mImpl->mCb.onProgress && uf->progressReportDue(SteadyTime::ds()))
                    mPool->mImpl->mCb.onProgress(uf->transfer(), uf->bytesConfirmed());
                if (handleBytesConfirmedOverflow(uf))
                    break;
            }
            mPool->mImpl->poolMgr.mActiveFiles.insert(uf);
            break;

        case WsApiServerEvent::FinalDataIngested:
            WSUPLOAD_TRACE << "[WsConn::onmessage] response->event == 7 final data ingested (server "
                         "knows file is complete) [chunk.len="
                      << chunk.len << "] [this = " << this << "]";
            if (chunk.len)
            {
                uf->onServerConfirmedBytes(chunk.len);
                // Goodput-saturation gate (SDK-5360): aggregate this pool's server-confirmed
                // bytes (same accumulator + discipline as the ChunkIngested arm above). Under
                // uploadMutex; guarded so gate=0 is byte-behaviour-identical.
                if (mPool->mImpl->mDatasetConnGate)
                    mPool->mConfirmedBytesTotal += static_cast<std::uint64_t>(chunk.len);
                // Tier 2 A (loss-recovery): record this whole-chunk range as acked so a
                // later reconnect re-queues only un-acked chunks (gated; clean no-op).
                if (mPool->mImpl->mAckedChunkRewind)
                    uf->markRangeAcked(chunk.pos, chunk.pos + chunk.len);
                if (handleBytesConfirmedOverflow(uf))
                    break;
            }
            if (mPool->mImpl->mCb.onProgress)
                mPool->mImpl->mCb.onProgress(uf->transfer(), uf->bytesConfirmed());
            mPool->mImpl->poolMgr.mActiveFiles.insert(uf);
            break;

        case WsApiServerEvent::AlreadyOnServer:
            WSUPLOAD_TRACE << "[WsConn::onmessage] response->event == 2 already on server (after "
                         "reconnect) [pos="
                      << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno
                      << "] [this = " << this << "]";
            // Tier 2 D-cancel-only (loss-recovery): the server already holds this chunk.
            // Purge any still-QUEUED resend for the exact (fileno,pos,len) so it is not
            // re-read + re-sent on the next drain (the loss amplifier). CANCEL ONLY -- no
            // byte credit, because the len-blind, per-conn, non-idempotent ack-match
            // (mBytesConfirmed += len) makes crediting on opcode-2 unsafe (it could
            // double-count against a late opcode-1 on the original conn). On a clean link
            // opcode-2 never fires, so this is a clean no-op.
            if (mPool->mImpl->mResendDedupCancel)
            {
                mPool->purgeQueuedResendForRangeLocked(chunk.fileno, chunk.pos, chunk.len);
            }
            break;

        case WsApiServerEvent::CrcFailed:
            LOG_warn << "[WsConn::onmessage] response->event == 3 CRC failed -> "
                        "mPool->retryChunk(chunk) [this = "
                     << this << "]";
            uf->onRequestFailed();
            mPool->retryChunkLocked(chunk);
            break;

        case WsApiServerEvent::UploadCompleted:
        {
            WSUPLOAD_TRACE << "[WsConn::onmessage] response->event == 4 upload completed -> "
                         "mPool->applyInFlight(response->fileno) [this = "
                      << this << "]";
            mPool->applyInFlightLocked(response->fileno);
            uf->maybeReportThroughput(mPool->mImpl->currentTime);

            // Completion frame layout is:
            // [ChunkResponse (13 bytes)] [payloadLen (1 byte)] [payload (N bytes)] [crc32 (4 bytes)]
            // Validate boundaries before reading payloadLen/payload.
            constexpr int kWsCompletionPayloadOffset =
                kWsChunkResponseHeaderSize + static_cast<int>(sizeof(std::uint8_t));
            constexpr int kCompletionPrefixLen = kWsCompletionPayloadOffset;
            constexpr int kTrailerCrcLen = detail::kInboundFrameTrailerCrcBytes;
            if (len < (kCompletionPrefixLen + kTrailerCrcLen))
            {
                LOG_warn << "WsUpload: invalid completion frame len=" << len;
                failFileLocked(uf,
                               response->fileno,
                               FailReason::Protocol,
                               API_EINTERNAL,
                               0,
                               UploadEngine::FailureDisposition::Retryable);
                break;
            }

            int payLen = static_cast<unsigned char>(msg[kWsChunkResponseHeaderSize]);
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
            if (globalMegaTestHooks.onWsUploadCompletionPayloadLen)
            {
                globalMegaTestHooks.onWsUploadCompletionPayloadLen(payLen);
            }
#endif
            const int maxPayloadLen = len - kCompletionPrefixLen - kTrailerCrcLen;
            if (payLen > maxPayloadLen)
            {
                LOG_warn << "WsUpload: invalid completion payload len=" << payLen
                         << " frame len=" << len;
                failFileLocked(uf,
                               response->fileno,
                               FailReason::Protocol,
                               API_EINTERNAL,
                               0,
                               UploadEngine::FailureDisposition::Retryable);
                break;
            }

#ifndef NDEBUG
            mPool->recordUploadCompletedLocked(response->fileno);
#endif
            uf->uploadCompleted(msg + kWsCompletionPayloadOffset, payLen);
            if (mPool->mImpl->mCb.onComplete)
            {
                const char* payload = (payLen > 0) ? (msg + kWsCompletionPayloadOffset) : nullptr;
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                // Test seam: allow integration tests to corrupt the upload-token
                // payload to exercise the invalid-token detach path in
                // MegaClient::onComplete.
                std::string payloadStr(payload ? payload : "",
                                       payload ? static_cast<size_t>(payLen) : 0u);
                int hookPayLen = payLen;
                DEBUG_TEST_HOOK_WSUPLOAD_CORRUPT_TOKEN(uf->fileno(), payloadStr, hookPayLen);
                mPool->mImpl->mCb.onComplete(uf->transfer(), payloadStr.data(), hookPayLen);
#else
                mPool->mImpl->mCb.onComplete(uf->transfer(), payload, payLen);
#endif
            }
            break;
        }

        case WsApiServerEvent::Distress:
            LOG_warn
                << "[WsConn::onmessage] response->event == 5 distress -> server requested pool "
                   "refresh -> refresh pools -> mPool->mImpl->poolMgr.refreshPools() [this = "
                << this << "]";
            mPool->mImpl->poolMgr.refreshPools();
            break;

        case WsApiServerEvent::Throttle:
        {
            const dstime throttleDs = static_cast<dstime>(chunkPos / 100 + 1);
#ifndef NDEBUG
            // Tag opcode=6 in code-counts and anchor pause start for recovery latency.
            if (mPool) ++mPool->mThrottleEventCodeCounts[6];
            mPauseStartedAtMs = static_cast<dstime>(SteadyTime::ds());
#endif
            // Release-safe bench-framework throttle counters. Relaxed memory
            // order — these are pure accumulators read at iter boundaries by
            // `UploadEngine::getAndResetBenchThrottleStats()` (no
            // happens-before relationship with surrounding state required).
            if (mPool)
            {
                mPool->mBenchThrottleEvent6Count.fetch_add(1, std::memory_order_relaxed);
                mPool->mBenchThrottleEvent6TotalMs.fetch_add(
                    static_cast<std::int64_t>(chunkPos), std::memory_order_relaxed);
                mPool->mBenchThrottlePauseCount.fetch_add(1, std::memory_order_relaxed);
                // throttleDs is ds (1/10s); convert to ms for the bench summary.
                mPool->mBenchThrottlePauseTotalMs.fetch_add(
                    static_cast<std::int64_t>(throttleDs) * 100, std::memory_order_relaxed);
            }
            LOG_info << "[WsUpload] Server requested sending to pause for " << chunkPos
                     << " ms";
            WSUPLOAD_TRACE << "[WsConn::onmessage] response->event == 6 throttle (ms) -> ds -> "
                         "mPool->pauseSending(throttleDs) [this = "
                      << this << "]";
            mPool->pauseSending(throttleDs);
            break;
        }

        default:
            WSUPLOAD_TRACE << "[WsConn::onmessage] response->event == "
                      << static_cast<int>(event)
                      << " -> unknown server opcode=" << static_cast<int>(event)
                      << " -> break [this = " << this << "]";
            break;
    }
    WSUPLOAD_TRACE << "[WsConn::onmessage] END [this = " << this << "]";
}

void WsConn::sendChunkData(const std::uint32_t fileno,
                           const m_off_t pos,
                           const char* data,
                           const int len)
{
    WSUPLOAD_TRACE << "[WsConn::sendChunkData] BEGIN [fileno=" << fileno << "] [pos=" << pos
              << "] [len=" << len << "] [this = " << this << "]";
    ChunkHeader header;
    header.fileno = fileno;
    header.pos = pos;
    header.len = len;
    header.crc = CRC32::crc32b(data,
                               len,
                               CRC32::crc32b(reinterpret_cast<const char*>(&header),
                                             static_cast<int>(offsetof(ChunkHeader, crc))));

    char bufIdx = mCurBuf;
    if (mBufs[static_cast<unsigned char>(bufIdx)].mDataLen)
        bufIdx = !bufIdx;

    WSUPLOAD_TRACE << "[WsConn::sendChunkData] senddata(bufIdx, reinterpret_cast<const char*>(&header), "
                 "static_cast<int>(sizeof header)) [bufIdx="
              << static_cast<int>(bufIdx) << "] [this = " << this << "]";
    senddata(bufIdx, reinterpret_cast<const char*>(&header), static_cast<int>(sizeof header));
    WSUPLOAD_TRACE << "[WsConn::sendChunkData] senddata(bufIdx, data, len) [bufIdx="
              << static_cast<int>(bufIdx) << "] [this = " << this << "]";
    senddata(bufIdx, data, len);
    WSUPLOAD_TRACE << "[WsConn::sendChunkData] END [this = " << this << "]";
}

// ========== WsPoolThread (ctor after WsPool complete) ==========
WsPoolThread::WsPoolThread(WsPool* pool):
    t(&WsPool::poolWorkerThread, pool, this)
{
    WSUPLOAD_TRACE << "[WsPoolThread::WsPoolThread] pool=" << (void*)pool << " [this = " << this << "]";
}

// ========== ChunkFingerprintMacUpdate ==========
// Out-of-line: the body calls WsUploadFile::queueConfirmedChunkMacs and so
// requires the full WsUploadFile definition (via ws_upload_file.h, included
// near the top of this file).
void ChunkFingerprintMacUpdate::apply(const m_off_t confirmedPos, WsUploadFile& file)
{
    if (confirmedPos != pos)
        return;
    if (macs.size())
    {
        file.queueConfirmedChunkMacs(std::move(macs));
    }
}

// ========== WsBuf ==========
bool WsBuf::sendWS(WsConn* ws, int& bufferedAmount)
{
    WSUPLOAD_TRACE << "[WsBuf::sendWS] BEGIN [this = " << this << "]";
    if (mSendPos >= mDataLen)
    {
        WSUPLOAD_TRACE << "[WsBuf::sendWS] mSendPos(=" << mSendPos << ") >= mDataLen(=" << mDataLen
                  << ") -> return false [this = " << this << "]";
        return false;
    }

    size_t sent = 0;
    WSUPLOAD_TRACE << "[WsBuf::sendWS] curl_ws_send(ws->curl, buf(=" << (void*)buf
              << ") + mSendPos(=" << mSendPos << ") = " << (void*)(buf + mSendPos)
              << ", mDataLen(=" << mDataLen << ") - mSendPos(=" << mSendPos
              << ") = " << (mDataLen - mSendPos) << ", &sent, 0, CURLWS_BINARY) [this = " << this
              << "]";
    std::size_t remaining = static_cast<std::size_t>(mDataLen - mSendPos);
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    // Fix #6 deterministic repro: ForcePartial truncates this curl_ws_send so the
    // SDK is left mid-frame; ForceDrop tears the connection down mid-frame (before
    // the continuation is presented on the same handle). See testhooks.h.
    {
        std::size_t forcedSendLen = 0;
        WsSendFaultAction faultAction = WsSendFaultAction::None;
        DEBUG_TEST_HOOK_WS_SEND_FAULT(remaining, forcedSendLen, faultAction);
        if (faultAction == WsSendFaultAction::ForceDrop)
        {
            WSUPLOAD_TRACE << "[WsBuf::sendWS] WS_SEND_FAULT ForceDrop -> ws->closeWS() mid-frame "
                              "[this = "
                           << this << "]";
            ws->closeWS();
            return false;
        }
        if (faultAction == WsSendFaultAction::ForcePartial && forcedSendLen > 0 &&
            forcedSendLen < remaining)
        {
            WSUPLOAD_TRACE << "[WsBuf::sendWS] WS_SEND_FAULT ForcePartial -> truncate remaining "
                           << remaining << " to " << forcedSendLen << " [this = " << this << "]";
            remaining = forcedSendLen;
        }
    }
#endif
    const CURLcode res = curl_ws_send(ws->curl, buf + mSendPos, remaining, &sent, 0, CURLWS_BINARY);
    if (res == CURLE_OK)
    {
        WSUPLOAD_TRACE << "[WsBuf::sendWS] res == CURLE_OK -> mSendPos(=" << mSendPos
                  << ") += static_cast<int>(sent(=" << sent
                  << ")), bufferedAmount(=" << bufferedAmount
                  << ") -= static_cast<int>(sent(=" << sent << ")) [this = " << this << "]";
        mSendPos += static_cast<int>(sent);
        bufferedAmount -= static_cast<int>(sent);
        // Ack-stall watchdog progress-guard (fu8 S6): bytes were ACCEPTED by curl_ws_send, i.e.
        // data is still flowing OUT of this conn. Refresh the send-progress stamp so the watchdog
        // does NOT force-reconnect a legitimately-slow-but-alive conn (rate-limited / low-bw).
        if (sent > 0)
            ws->mLastSendProgressDs.store(SteadyTime::ds(), std::memory_order_relaxed);
#ifndef NDEBUG
        // E-3: accumulate the bytes curl_ws_send accepted on this conn.
        ws->mTotalCurlWsSendAcceptedBytes += static_cast<std::uint64_t>(sent);
#endif
        if (mSendPos == mDataLen)
        {
            WSUPLOAD_TRACE << "[WsBuf::sendWS] mSendPos(=" << mSendPos << ") == mDataLen(=" << mDataLen
                      << ") -> reset() && return true [this = " << this << "]";
            reset();
            return true;
        }
        WSUPLOAD_TRACE << "[WsBuf::sendWS] mSendPos(=" << mSendPos << ") != mDataLen(=" << mDataLen
                  << ") -> return false [this = " << this << "]";
        // Fix #6: curl_ws_send accepted only part of this frame. The libcurl easy now
        // holds in-progress frame state that cannot continue on a fresh handle, so this
        // frame must be torn down (not reconnected) by closeWS().
        mFrameInProgress = true;
        return false;
    }
    if (res != CURLE_AGAIN)
    {
        WSUPLOAD_TRACE << "[WsBuf::sendWS] res(=" << res << ") != CURLE_AGAIN(=" << CURLE_AGAIN
                  << ") -> ws->closeWS() [this = " << this << "]";
        ws->closeWS();
    }
#ifndef NDEBUG
    else
    {
        // Count CURLE_AGAIN on send (per-conn).
        ++ws->mCurlAgainSendCount;
    }
#endif
    WSUPLOAD_TRACE << "[WsBuf::sendWS] res(=" << res << ") != CURLE_OK -> return false [this = " << this
              << "]";
    return false;
}

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD
