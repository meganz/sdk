/**
 * @file mega/testhooks.h
 * @brief helper classes for test cases to simulate various errors and special conditions
 *
 * (c) 2013-2017 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef MEGA_TESTHOOKS_H
#define MEGA_TESTHOOKS_H 1

#include "types.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mega {

    // These hooks allow the sdk_test project to simulate some error / retry conditions, or cause smaller download block sizes for quicker tests
    // However they do require some (minimal) extra code in the SDK.
    // The preprocessor is used to ensure that code is not present for release builds, so it can't cause problems.
    // Additionally the hooks use std::function so a suitable compiler and library are needed to leverage those tests.

#ifndef NDEBUG
    #define MEGASDK_DEBUG_TEST_HOOKS_ENABLED
#endif

    // Forward declarations, enum, sub-struct, MegaTestHooks struct, and the
    // globalMegaTestHooks extern are ALWAYS compiled. Only the
    // DEBUG_TEST_HOOK_* macros below are gated on
    // MEGASDK_DEBUG_TEST_HOOKS_ENABLED — they expand to nothing in Release.
    // This keeps the struct ABI-stable across NDEBUG/non-NDEBUG, so test
    // code (and helpers like WsUploadDebugHelpers.h) can reference the
    // fields under both configurations; assignment in Release is a no-op
    // because the macro at the SDK call site emits zero instructions.

    struct MEGA_API HttpReq;
    class MEGA_API RaidBufferManager;
    class DebugTestHook;
    struct Transfer;
    class TransferDbCommitter;

    namespace ws
    {
        struct WsConn;
        struct WsPool;
    } // namespace ws

    enum class WsUploadServerEventAction
    {
        None = 0,
        Drop = 1,
        Modify = 2,
    };

    struct WsUploadServerEventHook
    {
        WsUploadServerEventHook() = default;
        WsUploadServerEventHook(const WsUploadServerEventHook&) = delete;
        WsUploadServerEventHook& operator=(const WsUploadServerEventHook&) = delete;

        WsUploadServerEventHook(WsUploadServerEventHook&& other) noexcept
        {
            std::lock_guard<std::mutex> g(other.mMutex);
            sourceEvent = other.sourceEvent;
            targetEvent = other.targetEvent;
            targetChunkPos = other.targetChunkPos;
            action = other.action;
            fileno = other.fileno;
            maxHits = other.maxHits;
            hitCount = other.hitCount;
            secondaryDropEvent = other.secondaryDropEvent;
            secondaryDropHits = other.secondaryDropHits;
        }

        WsUploadServerEventHook& operator=(WsUploadServerEventHook&& other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }

            std::scoped_lock lk(mMutex, other.mMutex);
            sourceEvent = other.sourceEvent;
            targetEvent = other.targetEvent;
            targetChunkPos = other.targetChunkPos;
            action = other.action;
            fileno = other.fileno;
            maxHits = other.maxHits;
            hitCount = other.hitCount;
            secondaryDropEvent = other.secondaryDropEvent;
            secondaryDropHits = other.secondaryDropHits;
            return *this;
        }

        void configure(WsUploadServerEventAction actionIn,
                       int sourceEventIn,
                       std::optional<int> targetEventIn = std::nullopt,
                       std::optional<std::uint32_t> filenoIn = std::nullopt,
                       std::optional<m_off_t> targetChunkPosIn = std::nullopt,
                       std::optional<int> maxHitsIn = std::nullopt)
        {
            std::lock_guard<std::mutex> g(mMutex);
            sourceEvent = sourceEventIn;
            targetEvent = targetEventIn;
            fileno = filenoIn;
            maxHits = maxHitsIn.value_or(1);
            targetChunkPos = targetChunkPosIn;
            action = actionIn;
            hitCount = 0;
        }

        // Secondary unconditional Drop rule: independent of the primary Modify/Drop
        // configure() above. Matches any inbound event whose id equals eventIdIn,
        // regardless of fileno or primary rule state. Used by B9 to suppress
        // out-of-band server event=5 Distress frames that would otherwise retire
        // the throttled pool mid-test.
        void configureSecondaryDrop(int eventIdIn)
        {
            std::lock_guard<std::mutex> g(mMutex);
            secondaryDropEvent = eventIdIn;
            secondaryDropHits = 0;
        }

        void reset()
        {
            std::lock_guard<std::mutex> g(mMutex);
            sourceEvent = 0;
            targetEvent.reset();
            targetChunkPos.reset();
            action = WsUploadServerEventAction::None;
            fileno.reset();
            maxHits = 1;
            hitCount = 0;
            secondaryDropEvent.reset();
            secondaryDropHits = 0;
        }

        int getHitCount() const
        {
            std::lock_guard<std::mutex> g(mMutex);
            return hitCount;
        }

        int getSecondaryDropHits() const
        {
            std::lock_guard<std::mutex> g(mMutex);
            return secondaryDropHits;
        }

        WsUploadServerEventAction evaluate(const std::uint32_t filenoIn,
                                           int& eventInOut,
                                           m_off_t& chunkPosInOut)
        {
            std::lock_guard<std::mutex> g(mMutex);

            // Secondary Drop rule runs first and is independent of the primary
            // action — needed so B9 can drop event=5 Distress while still
            // running a one-shot Modify on event=1.
            if (secondaryDropEvent.has_value() && *secondaryDropEvent == eventInOut)
            {
                ++secondaryDropHits;
                return WsUploadServerEventAction::Drop;
            }

            if (action == WsUploadServerEventAction::None || sourceEvent != eventInOut ||
                (fileno.has_value() && *fileno != filenoIn) ||
                (maxHits != 0 && hitCount >= maxHits))
            {
                return WsUploadServerEventAction::None;
            }

            ++hitCount;
            if (action == WsUploadServerEventAction::Modify)
            {
                if (targetEvent.has_value())
                {
                    eventInOut = *targetEvent;
                }
                if (targetChunkPos.has_value())
                {
                    chunkPosInOut = *targetChunkPos;
                }
            }
            return action;
        }

    private:
        mutable std::mutex mMutex;
        WsUploadServerEventAction action = WsUploadServerEventAction::None;
        int sourceEvent = 0;
        std::optional<int> targetEvent;
        std::optional<m_off_t> targetChunkPos;
        std::optional<std::uint32_t> fileno;
        int maxHits = 1;
        int hitCount = 0;
        std::optional<int> secondaryDropEvent;
        int secondaryDropHits = 0;
    };

    // Fault-injection actions for WsBuf::sendWS (fix #6 deterministic repro).
    enum class WsSendFaultAction
    {
        None = 0,
        // Truncate the next curl_ws_send to a partial length so the SDK is left
        // mid-frame (mSendPos advanced but mSendPos < mDataLen, return false).
        ForcePartial = 1,
        // Tear the connection down (closeWS) while a frame is mid-flight, before
        // its continuation is presented on the same handle.
        ForceDrop = 2,
    };

    // Self-locked, always-compiled fault hook (modeled on WsUploadServerEventHook).
    // Only the DEBUG_TEST_HOOK_WS_SEND_FAULT macro is gated on
    // MEGASDK_DEBUG_TEST_HOOKS_ENABLED. Drives the only deterministic mid-frame
    // trigger: ForcePartial leaves a half-sent frame, then ForceDrop closes the
    // socket between the partial curl_ws_send and its continuation (the existing
    // DEBUG_TEST_HOOK_WSCONN_FORCE_CLOSE_NOW fires only BETWEEN send passes, never
    // inside a partial frame). See fix #6 §6.3.
    struct WsSendFaultHook
    {
        WsSendFaultHook() = default;
        WsSendFaultHook(const WsSendFaultHook&) = delete;
        WsSendFaultHook& operator=(const WsSendFaultHook&) = delete;

        WsSendFaultHook(WsSendFaultHook&& other) noexcept
        {
            std::lock_guard<std::mutex> g(other.mMutex);
            mEnabled = other.mEnabled;
            mPartialBytes = other.mPartialBytes;
            mDropAfterPartial = other.mDropAfterPartial;
            mPartialHits = other.mPartialHits;
            mDropHits = other.mDropHits;
            mSawPartial = other.mSawPartial;
        }

        WsSendFaultHook& operator=(WsSendFaultHook&& other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }
            std::scoped_lock lk(mMutex, other.mMutex);
            mEnabled = other.mEnabled;
            mPartialBytes = other.mPartialBytes;
            mDropAfterPartial = other.mDropAfterPartial;
            mPartialHits = other.mPartialHits;
            mDropHits = other.mDropHits;
            mSawPartial = other.mSawPartial;
            return *this;
        }

        // partialBytes: how many bytes curl_ws_send is allowed to take on the first
        // pass (must be >=1; the rest stays unsent so the SDK is mid-frame).
        // dropAfterPartial: if true, the pass immediately AFTER the forced partial
        // tears the connection down (closeWS) instead of continuing the frame.
        void configure(std::size_t partialBytes, bool dropAfterPartial)
        {
            std::lock_guard<std::mutex> g(mMutex);
            mEnabled = true;
            mPartialBytes = partialBytes ? partialBytes : 1;
            mDropAfterPartial = dropAfterPartial;
            mPartialHits = 0;
            mDropHits = 0;
            mSawPartial = false;
        }

        void reset()
        {
            std::lock_guard<std::mutex> g(mMutex);
            mEnabled = false;
            mPartialBytes = 0;
            mDropAfterPartial = false;
            mPartialHits = 0;
            mDropHits = 0;
            mSawPartial = false;
        }

        int getPartialHits() const
        {
            std::lock_guard<std::mutex> g(mMutex);
            return mPartialHits;
        }

        int getDropHits() const
        {
            std::lock_guard<std::mutex> g(mMutex);
            return mDropHits;
        }

        // Called inside WsBuf::sendWS after `remaining` is computed and before
        // curl_ws_send. `remaining` is the full unsent length of the current frame.
        // Returns the action; for ForcePartial sets forcedSendLen in [1, remaining-1].
        WsSendFaultAction evaluate(std::size_t remaining, std::size_t& forcedSendLen)
        {
            std::lock_guard<std::mutex> g(mMutex);
            if (!mEnabled || remaining <= 1)
            {
                return WsSendFaultAction::None;
            }

            // Order matters: once a partial has been forced, the NEXT pass drops
            // (mid-frame), then the hook disarms itself (one partial + one drop).
            if (mSawPartial)
            {
                if (mDropAfterPartial)
                {
                    mSawPartial = false;
                    mEnabled = false;
                    ++mDropHits;
                    return WsSendFaultAction::ForceDrop;
                }
                return WsSendFaultAction::None;
            }

            std::size_t take = mPartialBytes;
            if (take >= remaining)
            {
                take = remaining - 1; // keep at least 1 byte unsent
            }
            if (take == 0)
            {
                return WsSendFaultAction::None;
            }
            forcedSendLen = take;
            mSawPartial = true;
            ++mPartialHits;
            return WsSendFaultAction::ForcePartial;
        }

    private:
        mutable std::mutex mMutex;
        bool mEnabled = false;
        std::size_t mPartialBytes = 0;
        bool mDropAfterPartial = false;
        int mPartialHits = 0;
        int mDropHits = 0;
        bool mSawPartial = false;
    };

    // Self-locked, always-compiled recv-swallow hook (SDK-5360 fu8 Session 6 ack-stall
    // repro). When armed, WsConn::curlRecv DISCARDS inbound server frames without running
    // onmessage(), so the per-conn liveness stamp (WsConn::mLastInboundFrameDs) and the
    // pool-level mLastServerResponse are never refreshed: a silent-but-OPEN server (acks
    // stop while the socket stays OPEN and the SDK keeps sending). This is the only way to
    // exercise the ack-stall watchdog deterministically — WsUploadServerEventAction::Drop
    // fires AFTER the liveness stamp, so it cannot freeze liveness. Optionally let
    // `startAfterFrames` frames through first (so >=1 chunk is acked before the stall,
    // matching "uploaded partway then hung"). Only the DEBUG_TEST_HOOK_WS_RECV_SWALLOW macro
    // is gated on MEGASDK_DEBUG_TEST_HOOKS_ENABLED. Own internal mutex + locked move, like
    // WsSendFaultHook above.
    struct WsRecvSwallowHook
    {
        WsRecvSwallowHook() = default;
        WsRecvSwallowHook(const WsRecvSwallowHook&) = delete;
        WsRecvSwallowHook& operator=(const WsRecvSwallowHook&) = delete;

        WsRecvSwallowHook(WsRecvSwallowHook&& other) noexcept
        {
            std::lock_guard<std::mutex> g(other.mMutex);
            mEnabled = other.mEnabled;
            mStartAfterFrames = other.mStartAfterFrames;
            mSwallowDurationMs = other.mSwallowDurationMs;
            mFramesSeen = other.mFramesSeen;
            mSwallowed = other.mSwallowed;
            mSwallowStarted = other.mSwallowStarted;
            mSwallowStartTp = other.mSwallowStartTp;
        }

        WsRecvSwallowHook& operator=(WsRecvSwallowHook&& other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }
            std::scoped_lock lk(mMutex, other.mMutex);
            mEnabled = other.mEnabled;
            mStartAfterFrames = other.mStartAfterFrames;
            mSwallowDurationMs = other.mSwallowDurationMs;
            mFramesSeen = other.mFramesSeen;
            mSwallowed = other.mSwallowed;
            mSwallowStarted = other.mSwallowStarted;
            mSwallowStartTp = other.mSwallowStartTp;
            return *this;
        }

        // startAfterFrames: inbound frames to let through before swallowing begins
        // (0 = swallow from the first frame).
        // swallowDurationMs: bounded swallow WINDOW (wall-clock ms, measured from the first
        // swallowed frame) after which swallowing STOPS and frames flow again — models a
        // TRANSIENT hang that then clears, so a watchdog-driven reconnect can recover the
        // upload (the transfer COMPLETES post-fix). 0 = swallow forever (permanent hang;
        // never recovers — use only for reconnect-attempt-counting tests, not completion).
        void configure(int startAfterFrames, int swallowDurationMs = 0)
        {
            std::lock_guard<std::mutex> g(mMutex);
            mEnabled = true;
            mStartAfterFrames = startAfterFrames < 0 ? 0 : startAfterFrames;
            mSwallowDurationMs = swallowDurationMs < 0 ? 0 : swallowDurationMs;
            mFramesSeen = 0;
            mSwallowed = 0;
            mSwallowStarted = false;
        }

        void reset()
        {
            std::lock_guard<std::mutex> g(mMutex);
            mEnabled = false;
            mStartAfterFrames = 0;
            mSwallowDurationMs = 0;
            mFramesSeen = 0;
            mSwallowed = 0;
            mSwallowStarted = false;
        }

        int getSwallowCount() const
        {
            std::lock_guard<std::mutex> g(mMutex);
            return mSwallowed;
        }

        // Called in WsConn::curlRecv just before onmessage(). Returns true to DISCARD the
        // frame (onmessage skipped). Swallowing begins after startAfterFrames frames and, if a
        // swallowDurationMs window was set, STOPS once that window elapses (frames flow again
        // so a reconnect can recover the upload); otherwise it continues until reset().
        bool shouldSwallow()
        {
            std::lock_guard<std::mutex> g(mMutex);
            if (!mEnabled)
            {
                return false;
            }
            if (mFramesSeen < mStartAfterFrames)
            {
                ++mFramesSeen;
                return false;
            }
            if (mSwallowDurationMs > 0)
            {
                const auto now = std::chrono::steady_clock::now();
                if (!mSwallowStarted)
                {
                    mSwallowStarted = true;
                    mSwallowStartTp = now;
                }
                const auto elapsedMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - mSwallowStartTp)
                        .count();
                if (elapsedMs >= mSwallowDurationMs)
                {
                    return false; // transient-hang window elapsed: let frames through again
                }
            }
            ++mSwallowed;
            return true;
        }

    private:
        mutable std::mutex mMutex;
        bool mEnabled = false;
        int mStartAfterFrames = 0;
        int mSwallowDurationMs = 0;
        int mFramesSeen = 0;
        int mSwallowed = 0;
        bool mSwallowStarted = false;
        std::chrono::steady_clock::time_point mSwallowStartTp;
    };

    struct MegaTestHooks
    {
        // O-13: guards whole-struct assignment (e.g. `globalMegaTestHooks = MegaTestHooks();`
        // at SdkTest_test.cpp:7368 and scoped-destructor reset sites) against concurrent
        // worker-thread reads via DEBUG_TEST_HOOK_* macros. Every macro below uses the
        // copy-under-lock / invoke-outside-lock idiom so that callbacks may themselves
        // reset hooks without deadlocking. The move ctor/assign below acquire both mutexes
        // (source + destination) atomically with std::scoped_lock.
        mutable std::mutex mMutex;

        MegaTestHooks() = default;

        MegaTestHooks(const MegaTestHooks&) = delete;
        MegaTestHooks& operator=(const MegaTestHooks&) = delete;

        MegaTestHooks(MegaTestHooks&& other) noexcept
        {
            std::scoped_lock g(other.mMutex);
            moveFieldsFrom(std::move(other));
        }

        MegaTestHooks& operator=(MegaTestHooks&& other) noexcept
        {
            if (this == &other)
                return *this;
            std::scoped_lock g(mMutex, other.mMutex);
            moveFieldsFrom(std::move(other));
            return *this;
        }

        std::function<bool(HttpReq*)> onHttpReqPost;
        std::function<void(RaidBufferManager*)> onSetIsRaid;
        std::function<void(error e)> onUploadChunkFailed;
        std::function<void(const m_off_t)> onProgressCompletedUpdate;
        std::function<void(const m_off_t)> onProgressContiguousUpdate;
        std::function<bool(Transfer*, TransferDbCommitter&)> onUploadChunkSucceeded;
        std::function<void(const double, const m_off_t, const m_off_t)> onTransferReportProgress;
        std::function<void(error e)> onDownloadFailed;
        // interceptSCRequest / interceptSCChunk: read-only from the client thread.
        // - globalMegaTestHooks.interceptSCRequest is read inside MegaClient::chooseScParsingMode()
        //   (src/megaclient.cpp:26446) which runs on the client thread.
        // - megaTestHooks.interceptSCChunk is a per-client member read inside
        //   MegaClient::handleScInStreaming() (src/megaclient.cpp:26722), also client thread.
        // No worker-thread reader exists, so these two fields intentionally have no
        // DEBUG_TEST_HOOK_* macro and are read without taking mMutex. The outer
        // move ctor/op= still include them so that `globalMegaTestHooks = MegaTestHooks();`
        // resets them atomically w.r.t. any future worker-thread reader that might be
        // introduced.
        std::function<void(std::unique_ptr<HttpReq>&)> interceptSCRequest;
        std::function<void(std::unique_ptr<HttpReq>&)> interceptSCChunk;
        // Called when an HTTP 1xx (e.g. the 103 heartbeat) is received, with the status code and
        // the receiving request's id. Used to observe server heartbeats from tests (the id lets a
        // test tell heartbeats on one request from another).
        std::function<void(int /*statusCode*/, uint32_t /*reqId*/)> onHeartbeatReceived;
        std::function<void(m_off_t&)> onLimitMaxReqSize;
        std::function<void(int&, unsigned)> onHookNumberOfConnections;
        std::function<void(bool&)> onHookDownloadRequestSingleUrl;
        std::function<void(m_time_t&)> onHookResetTransferLastAccessTime;
        std::function<void(std::unique_ptr<HttpReq>&)> interceptLocklessCSRequest;
        std::function<void(HttpReq*)> interceptCSRequest;
        std::function<
            void(const int /*httpStatus*/, const unsigned /*curlCode*/, const bool /*failed*/)>
            onHttpReqFinish;
        std::function<bool(const std::string&, long, std::string&)> onWsHandshake;
        std::function<void(dstime&)> onWsUploadSustainedHandshakeFailureWindowDs;
        std::function<void(const char* /*reason*/, bool /*stillTracked*/)>
            onWsUploadFailureDetached;
        std::function<bool(std::uint32_t /*fileno*/, std::string& /*payload*/)>
            onWsUploadCorruptToken;
        std::function<void(int /*tag*/)> onUploadPutnodesStarted;
        // B9 regression hooks. Read/written only from the pool worker thread + test
        // thread; assignment to either field must go through globalMegaTestHooks like
        // every other hook. Returning true from onWsConnForceCloseNow requests that the
        // caller transition the WsConn to CLOSED before the next loop iteration.
        std::function<bool(ws::WsConn* /*conn*/,
                           ws::WsPool* /*pool*/,
                           const std::string& /*poolUrl*/)>
            onWsConnForceCloseNow;
        std::function<void(ws::WsPool* /*pool*/,
                           unsigned /*retryCount*/,
                           dstime /*firstFailureDs*/)>
            onWsPoolReconnectAttempt;
        // Fires when the ack-stall watchdog (WsPoolMgr::checkPools) force-reconnects a
        // silently-hung OPEN connection (SDK-5360 fu8 S6). Lets a test count watchdog firings
        // UNAMBIGUOUSLY (distinct from onWsPoolReconnectAttempt, which also fires on
        // cold-start handshakes). Read-only from the WS engine thread under uploadMutex.
        std::function<void(ws::WsConn* /*conn*/, ws::WsPool* /*pool*/)>
            onWsAckStallForceReconnect;
        // Fires when the tail-completion watchdog (SDK-5360 fu8 S9) fails a bytes-complete-
        // but-completionless file for retry (WsPoolMgr::checkPools). Lets a test observe the
        // recovery deterministically. Fires on the WS engine thread under uploadMutex.
        std::function<void(std::uint32_t /*fileno*/, ws::WsPool* /*pool*/)>
            onWsTailCompletionRecovery;
        // Returns true to DROP an upload-completion frame for this fileno (one-shot frame-loss
        // simulation for the fu8 S9 tail-completion watchdog test): recordUploadCompleted/
        // uploadCompleted/onComplete are all skipped, leaving the file bytes-confirmed but
        // completionless — the exact wedge the watchdog recovers. WS engine thread.
        std::function<bool(std::uint32_t /*fileno*/)> onWsUploadDropCompletion;
        // Fires when Transfer::ws_session_url is rewritten by client-thread bookkeeping
        // (onStart re-population or invalidatePinnedSessionUrl clearing). Used by the
        // InvalidPinned test to deterministically observe WS pool transitions instead
        // of polling. Fires on the client thread, after any engine mutex release.
        std::function<void(int /*transferTag*/,
                           const std::string& /*oldUrl*/,
                           const std::string& /*newUrl*/,
                           const char* /*reason*/)>
            onWsSessionUrlTransition;
        // Fires inside WsPool::sendChunk just before a chunk is enqueued in mChunksInFlight
        // and written to the wire. Returning true requests that the chunk-send be
        // short-circuited and the transfer driven through its OVERQUOTA failure path
        // (mCb.onFail(t, API_EOVERQUOTA, 0, Retryable) → Transfer::failed(API_EOVERQUOTA)
        // → activateoverquota(0, false) → app->transfer_failed(t, API_EOVERQUOTA, 0)).
        // Hook receives the transfer's tag so tests can inject on a specific transfer.
        // Used by SdkWsUploadTest.OverquotaDuringTransfer to deterministically exercise
        // the WS-channel OVERQUOTA path without depending on staging quota state.
        std::function<bool(int /*transferTag*/)> onWsChunkSendOverquota;
        // Goodput-saturation gate v2 (SDK-5360 QCT-K) controller-input seams. Both fire on the
        // WS engine thread under uploadMutex, inside WsPool::runGoodputGateLocked, so a test can
        // drive the ramp controller with a deterministic synthetic signal while the REAL send
        // path keeps flowing (the hooks override only the controller's VIEW, not the wire):
        //  - onWsGateBackpressureSample: per-tick backpressure sampler (step A). `open` is the
        //    live OPEN-conn count; the test writes `bp` (the backpressured count) to forge a
        //    quorum (e.g. bp=open => 100 %) independent of worker send-buffer timing.
        //  - onWsGateGoodputSample: goodput judge (window baseline + probe horizon). The test
        //    writes `bps` from a synthetic conns->goodput curve to prove the gain-judged stop
        //    below the ceiling and the knee retreat without depending on real link behaviour.
        // `pool` is an opaque WsPool identity token (const void*), matching the controller's
        // HR23 discipline — the hooks never dereference pool state, so no full type is needed.
        std::function<void(const void* /*pool*/, unsigned /*open*/, unsigned& /*bp*/)>
            onWsGateBackpressureSample;
        std::function<void(const void* /*pool*/, unsigned /*conns*/, double& /*bps*/)>
            onWsGateGoodputSample;
        // WsUploadServerEventHook has its own internal mutex and its own locked move
        // ctor/op=; the outer mMutex keeps the enclosing struct move atomic, and the
        // sub-object's mutex keeps its fields safe for evaluate() from any thread.
        WsUploadServerEventHook wsUploadServerEventHook;
        // Self-locked WS send-fault hook (fix #6 deterministic repro); own internal
        // mutex + locked move, like wsUploadServerEventHook above.
        WsSendFaultHook wsSendFaultHook;
        // Self-locked WS recv-swallow hook (fu8 S6 ack-stall repro); own internal mutex +
        // locked move, like wsSendFaultHook above.
        WsRecvSwallowHook wsRecvSwallowHook;
        // Allows tests to override completion payload length observed by WS upload handling.
        std::function<void(int&)> onWsUploadCompletionPayloadLen;
        // Allow tests to override the ack-stall / tail-completion watchdog windows at their
        // checkPools read sites (HR58: tests must pass with NO env; the env knobs are
        // read-once statics seeded at engine construction, which happens at login — before a
        // test body can install hooks — so a runtime seam is required for self-configuration).
        std::function<void(dstime&)> onWsUploadAckStallTimeoutDs;
        std::function<void(dstime&)> onWsUploadTailCompletionTimeoutDs;

        // Allow tests to force legacy (buggy) sparse CRC offset computation in FileFingerprint.
        // When enabled, FileFingerprint uses `legacySparseOffset32Bug()` instead of the fixed
        // 64-bit math when sampling large files (sparse CRC).
        std::function<void(bool&)> onHookFileFingerprintUseLegacyBuggySparseCrc;
        // Allow to set device id to a specific value for testing purposes
        std::function<void(std::string& deviceId)> onHookDeviceId;
        std::function<void()> onHashcashCalculationStarted;
        // Called during generateMetaMac after reading each chunk. Allows tests to modify/lock
        // the file mid-computation to trigger read errors.
        std::function<void(const m_off_t currentOffset)> onMacGenerationChunkRead;

        // Folder-upload testing: force per-node putnodes errors in the folder-creation
        // completion callback, to exercise partial-failure handling (some nodes fail while
        // the overall command returns API_OK).
        std::function<void(std::vector<NewNode>&)> onFolderUploadPutnodesResult;

        // Folder-upload testing: make an already-sent folder look missing on re-lookup, to
        // exercise the "created earlier but gone now" guard (e.g. deleted by another session).
        // Returns true to force the folder to be treated as not found.
        std::function<bool(const std::string& folderName)> onFolderUploadSimulateMissing;
    
    private:
        // Helper used by the move ctor/op=; callers MUST already hold the relevant mutex(es).
        // Must mention EVERY field above — missing one leaks on reset.
        void moveFieldsFrom(MegaTestHooks&& other) noexcept
        {
            onHttpReqPost = std::move(other.onHttpReqPost);
            onSetIsRaid = std::move(other.onSetIsRaid);
            onUploadChunkFailed = std::move(other.onUploadChunkFailed);
            onProgressCompletedUpdate = std::move(other.onProgressCompletedUpdate);
            onProgressContiguousUpdate = std::move(other.onProgressContiguousUpdate);
            onUploadChunkSucceeded = std::move(other.onUploadChunkSucceeded);
            onTransferReportProgress = std::move(other.onTransferReportProgress);
            onDownloadFailed = std::move(other.onDownloadFailed);
            interceptSCRequest = std::move(other.interceptSCRequest);
            interceptSCChunk = std::move(other.interceptSCChunk);
            onLimitMaxReqSize = std::move(other.onLimitMaxReqSize);
            onHookNumberOfConnections = std::move(other.onHookNumberOfConnections);
            onHookDownloadRequestSingleUrl = std::move(other.onHookDownloadRequestSingleUrl);
            onHookResetTransferLastAccessTime = std::move(other.onHookResetTransferLastAccessTime);
            interceptLocklessCSRequest = std::move(other.interceptLocklessCSRequest);
            onHttpReqFinish = std::move(other.onHttpReqFinish);
            onWsHandshake = std::move(other.onWsHandshake);
            onWsUploadSustainedHandshakeFailureWindowDs =
                std::move(other.onWsUploadSustainedHandshakeFailureWindowDs);
            onWsUploadFailureDetached = std::move(other.onWsUploadFailureDetached);
            onWsUploadCorruptToken = std::move(other.onWsUploadCorruptToken);
            onUploadPutnodesStarted = std::move(other.onUploadPutnodesStarted);
            onWsConnForceCloseNow = std::move(other.onWsConnForceCloseNow);
            onWsPoolReconnectAttempt = std::move(other.onWsPoolReconnectAttempt);
            onWsAckStallForceReconnect = std::move(other.onWsAckStallForceReconnect);
            onWsTailCompletionRecovery = std::move(other.onWsTailCompletionRecovery);
            onWsUploadDropCompletion = std::move(other.onWsUploadDropCompletion);
            onWsUploadCompletionPayloadLen = std::move(other.onWsUploadCompletionPayloadLen);
            onWsUploadAckStallTimeoutDs = std::move(other.onWsUploadAckStallTimeoutDs);
            onWsUploadTailCompletionTimeoutDs =
                std::move(other.onWsUploadTailCompletionTimeoutDs);
            onWsSessionUrlTransition = std::move(other.onWsSessionUrlTransition);
            onWsChunkSendOverquota = std::move(other.onWsChunkSendOverquota);
            onWsGateBackpressureSample = std::move(other.onWsGateBackpressureSample);
            onWsGateGoodputSample = std::move(other.onWsGateGoodputSample);
            // WsUploadServerEventHook already has its own locked move-assign.
            wsUploadServerEventHook = std::move(other.wsUploadServerEventHook);
            // WsSendFaultHook likewise has its own locked move-assign.
            wsSendFaultHook = std::move(other.wsSendFaultHook);
            // WsRecvSwallowHook likewise has its own locked move-assign.
            wsRecvSwallowHook = std::move(other.wsRecvSwallowHook);
            onHookFileFingerprintUseLegacyBuggySparseCrc =
                std::move(other.onHookFileFingerprintUseLegacyBuggySparseCrc);
            onHookDeviceId = std::move(other.onHookDeviceId);
            onHashcashCalculationStarted = std::move(other.onHashcashCalculationStarted);
            onMacGenerationChunkRead = std::move(other.onMacGenerationChunkRead);
            onFolderUploadPutnodesResult = std::move(other.onFolderUploadPutnodesResult);
            onFolderUploadSimulateMissing = std::move(other.onFolderUploadSimulateMissing);
        }
    };

    extern MegaTestHooks globalMegaTestHooks;

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED

    // O-13: all DEBUG_TEST_HOOK_* macros below use the copy-under-lock / invoke-outside-lock
    // idiom. They take a local copy of the std::function while holding globalMegaTestHooks.mMutex,
    // release the lock, then invoke the copy. This serializes reads against the whole-struct
    // assignment `globalMegaTestHooks = MegaTestHooks();` without holding the lock across the
    // callback (which prevents deadlock if the callback itself mutates globalMegaTestHooks).

    // allow the test client to skip an actual http request, and set the results directly.  The return statement, if activated, skips the http post()
    #define DEBUG_TEST_HOOK_HTTPREQ_POST(HTTPREQPTR) \
    do { \
        std::function<bool(HttpReq*)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onHttpReqPost; \
        } \
        if (_fn && _fn(HTTPREQPTR)) return; \
    } while (0)

    // allow the test client to confirm raid/nonraid is happening, or adjust the parameters of a raid download for smaller chunks etc
    #define DEBUG_TEST_HOOK_RAIDBUFFERMANAGER_SETISRAID(RAIDBUFMGRPTR) \
    do { \
        std::function<void(RaidBufferManager*)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onSetIsRaid; \
        } \
        if (_fn) _fn(RAIDBUFMGRPTR); \
    } while (0)

    // watch out for upload issues
    #define DEBUG_TEST_HOOK_UPLOADCHUNK_FAILED(X) \
    do { \
        std::function<void(error)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onUploadChunkFailed; \
        } \
        if (_fn) _fn(X); \
    } while (0)

    // option to simulate something after an uploaded chunk. Preserves the early-exit
    // semantics: if the hook returns false, the caller's enclosing function returns.
    #define DEBUG_TEST_HOOK_UPLOADCHUNK_SUCCEEDED(transfer, committer) \
    do { \
        std::function<bool(Transfer*, TransferDbCommitter&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onUploadChunkSucceeded; \
        } \
        if (_fn && !_fn((transfer), (committer))) return; \
    } while (0)

    // get transfer progress completed updates
#define DEBUG_TEST_HOOK_ON_PROGRESS_COMPLETED_UPDATE(p) \
    do { \
        std::function<void(const m_off_t)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onProgressCompletedUpdate; \
        } \
        if (_fn) _fn(p); \
    } while (0)

    // get transfer progress contiguous updates
#define DEBUG_TEST_HOOK_ON_PROGRESS_CONTIGUOUS_UPDATE(p) \
    do { \
        std::function<void(const m_off_t)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onProgressContiguousUpdate; \
        } \
        if (_fn) _fn(p); \
    } while (0)

    // get reports counts updates
#define DEBUG_TEST_HOOK_ON_TRANSFER_REPORT_PROGRESS(p, fp, pb) \
    do { \
        std::function<void(const double, const m_off_t, const m_off_t)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onTransferReportProgress; \
        } \
        if (_fn) _fn((p), (fp), (pb)); \
    } while (0)

    // watch out for download issues
    #define DEBUG_TEST_HOOK_DOWNLOAD_FAILED(X) \
    do { \
        std::function<void(error)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onDownloadFailed; \
        } \
        if (_fn) _fn(X); \
    } while (0)

    // limit max request size for TransferBufferManager (non-raid) or new RaidReq
    #define DEBUG_TEST_HOOK_LIMIT_MAX_REQ_SIZE(X) \
    do { \
        std::function<void(m_off_t&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onLimitMaxReqSize; \
        } \
        if (_fn) _fn(X); \
    } while (0)

    // Ensure new RaidReq number of connections is taken from the client's number of connections
    #define DEBUG_TEST_HOOK_NUMBER_OF_CONNECTIONS(connectionsInOutVar, clientNumberOfConnections) \
    do { \
        std::function<void(int&, unsigned)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onHookNumberOfConnections; \
        } \
        if (_fn) _fn((connectionsInOutVar), (clientNumberOfConnections)); \
    } while (0)

    // For CommandGetFile, so a raided file can request the unraided copy.
#define DEBUG_TEST_HOOK_DOWNLOAD_REQUEST_SINGLEURL(singleUrlFlag) \
    do { \
        std::function<void(bool&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onHookDownloadRequestSingleUrl; \
        } \
        if (_fn) _fn(singleUrlFlag); \
    } while (0)

#define DEBUG_TEST_HOOK_RESET_TRANSFER_LASTACCESSTIME(lastAccessTime) \
    do { \
        std::function<void(m_time_t&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onHookResetTransferLastAccessTime; \
        } \
        if (_fn) _fn(lastAccessTime); \
    } while (0)

#define DEBUG_TEST_HOOK_INTERCEPT_LOCKLESS_CS_REQUEST(pendingLocklessCS) \
    do { \
        std::function<void(std::unique_ptr<HttpReq>&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.interceptLocklessCSRequest; \
        } \
        if (_fn) _fn(pendingLocklessCS); \
    } while (0)

#define DEBUG_TEST_HOOK_INTERCEPT_CS_REQUEST(pendingCS) \
        { \
            if (globalMegaTestHooks.interceptCSRequest) \
                globalMegaTestHooks.interceptCSRequest(pendingCS); \
        }

#define DEBUG_TEST_HOOK_HTTPREQ_FINISH(HTTPSTATUS, CURLCODE, FAILED) \
    do { \
        std::function<void(const int, const unsigned, const bool)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onHttpReqFinish; \
        } \
        if (_fn) _fn((HTTPSTATUS), (CURLCODE), (FAILED)); \
    } while (0)

#define DEBUG_TEST_HOOK_HEARTBEAT_RECEIVED(STATUSCODE, REQID) \
        { \
            if (globalMegaTestHooks.onHeartbeatReceived) \
                globalMegaTestHooks.onHeartbeatReceived((STATUSCODE), (REQID)); \
        }

#define DEBUG_TEST_HOOK_WS_HANDSHAKE(URL, TIMEOUTMS, ERRSTRING, SHOULDFAIL) \
    do { \
        std::function<bool(const std::string&, long, std::string&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsHandshake; \
        } \
        if (_fn) (SHOULDFAIL) = _fn((URL), (TIMEOUTMS), (ERRSTRING)); \
    } while (0)

#define DEBUG_TEST_HOOK_WSUPLOAD_SUSTAINED_HANDSHAKE_FAILURE_WINDOW_DS(WINDOWDS) \
    do { \
        std::function<void(dstime&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsUploadSustainedHandshakeFailureWindowDs; \
        } \
        if (_fn) _fn((WINDOWDS)); \
    } while (0)

#define DEBUG_TEST_HOOK_WSUPLOAD_ACKSTALL_TIMEOUT_DS(WINDOWDS) \
    do { \
        std::function<void(dstime&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsUploadAckStallTimeoutDs; \
        } \
        if (_fn) _fn((WINDOWDS)); \
    } while (0)

#define DEBUG_TEST_HOOK_WSUPLOAD_TAILCOMPLETION_TIMEOUT_DS(WINDOWDS) \
    do { \
        std::function<void(dstime&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsUploadTailCompletionTimeoutDs; \
        } \
        if (_fn) _fn((WINDOWDS)); \
    } while (0)

#define DEBUG_TEST_HOOK_WSUPLOAD_COMPLETION_PAYLOAD_LEN(PAYLEN) \
    do { \
        std::function<void(int&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsUploadCompletionPayloadLen; \
        } \
        if (_fn) _fn((PAYLEN)); \
    } while (0)

#define DEBUG_TEST_HOOK_WSUPLOAD_FAILURE_DETACHED(REASON, STILL_TRACKED) \
    do { \
        std::function<void(const char*, bool)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsUploadFailureDetached; \
        } \
        if (_fn) _fn((REASON), (STILL_TRACKED)); \
    } while (0)

#define DEBUG_TEST_HOOK_WSUPLOAD_CORRUPT_TOKEN(FILENO, PAYLOAD, PAYLEN) \
    do { \
        std::function<bool(std::uint32_t, std::string&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsUploadCorruptToken; \
        } \
        if (_fn && _fn((FILENO), (PAYLOAD))) \
            (PAYLEN) = static_cast<int>((PAYLOAD).size()); \
    } while (0)

#define DEBUG_TEST_HOOK_UPLOAD_PUTNODES_STARTED(TAG) \
    do { \
        std::function<void(int)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onUploadPutnodesStarted; \
        } \
        if (_fn) _fn((TAG)); \
    } while (0)

// WSUPLOAD_SERVER_EVENT reads the self-locked WsUploadServerEventHook sub-object.
// evaluate() takes its own internal mutex, so no outer lock is needed here. The enclosing
// struct's move ctor/op= still acquire mMutex to keep the whole-struct assignment atomic.
#define DEBUG_TEST_HOOK_WSUPLOAD_SERVER_EVENT(FILENO, EVENT, CHUNKPOS, RESULT) \
        { \
            (RESULT) = globalMegaTestHooks.wsUploadServerEventHook.evaluate((FILENO), \
                                                                            (EVENT), \
                                                                            (CHUNKPOS)); \
        }

// WS_SEND_FAULT reads the self-locked WsSendFaultHook sub-object (own internal
// mutex), so no outer lock is needed. REMAINING is the current frame's unsent
// length; on ForcePartial, FORCEDLEN receives a value in [1, REMAINING-1].
#define DEBUG_TEST_HOOK_WS_SEND_FAULT(REMAINING, FORCEDLEN, RESULT) \
        { \
            (RESULT) = globalMegaTestHooks.wsSendFaultHook.evaluate((REMAINING), (FORCEDLEN)); \
        }

#define DEBUG_TEST_HOOK_WS_RECV_SWALLOW(OUTBOOL) \
        { \
            (OUTBOOL) = globalMegaTestHooks.wsRecvSwallowHook.shouldSwallow(); \
        }

#define DEBUG_TEST_HOOK_WSCONN_FORCE_CLOSE_NOW(CONNPTR, POOLPTR, POOLURL, OUTBOOL) \
    do { \
        std::function<bool(ws::WsConn*, ws::WsPool*, const std::string&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsConnForceCloseNow; \
        } \
        if (_fn) (OUTBOOL) = _fn((CONNPTR), (POOLPTR), (POOLURL)); \
    } while (0)

#define DEBUG_TEST_HOOK_WSPOOL_RECONNECT_ATTEMPT(POOLPTR, RETRYCOUNT, FIRSTFAILUREDS) \
    do { \
        std::function<void(ws::WsPool*, unsigned, dstime)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsPoolReconnectAttempt; \
        } \
        if (_fn) _fn((POOLPTR), (RETRYCOUNT), (FIRSTFAILUREDS)); \
    } while (0)

#define DEBUG_TEST_HOOK_WS_ACKSTALL_FORCE_RECONNECT(CONNPTR, POOLPTR) \
    do { \
        std::function<void(ws::WsConn*, ws::WsPool*)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsAckStallForceReconnect; \
        } \
        if (_fn) _fn((CONNPTR), (POOLPTR)); \
    } while (0)

#define DEBUG_TEST_HOOK_WS_TAILCOMPLETION_RECOVERY(FILENO, POOLPTR) \
    do { \
        std::function<void(std::uint32_t, ws::WsPool*)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsTailCompletionRecovery; \
        } \
        if (_fn) _fn((FILENO), (POOLPTR)); \
    } while (0)

#define DEBUG_TEST_HOOK_WSUPLOAD_DROP_COMPLETION(FILENO, DROPPED) \
    do { \
        std::function<bool(std::uint32_t)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsUploadDropCompletion; \
        } \
        if (_fn) (DROPPED) = _fn((FILENO)); \
    } while (0)

#define DEBUG_TEST_HOOK_WS_SESSION_URL_TRANSITION(TAG, OLDURL, NEWURL, REASON) \
    do { \
        std::function<void(int, const std::string&, const std::string&, const char*)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsSessionUrlTransition; \
        } \
        if (_fn) _fn((TAG), (OLDURL), (NEWURL), (REASON)); \
    } while (0)

#define DEBUG_TEST_HOOK_WS_CHUNK_SEND_OVERQUOTA(TAG, OUT_INJECT_OVERQUOTA) \
    do { \
        std::function<bool(int)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsChunkSendOverquota; \
        } \
        if (_fn) (OUT_INJECT_OVERQUOTA) = _fn((TAG)); \
    } while (0)

// Goodput-gate v2 controller-input seams (SDK-5360 QCT-K). Copy-under-lock / invoke-outside-
// lock like every other hook; the callbacks only write their out-params, so they cannot
// re-enter uploadMutex. POOLPTR is passed as a const void* identity token.
#define DEBUG_TEST_HOOK_WS_GATE_BP_SAMPLE(POOLPTR, OPEN, BP) \
    do { \
        std::function<void(const void*, unsigned, unsigned&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsGateBackpressureSample; \
        } \
        if (_fn) _fn((POOLPTR), (OPEN), (BP)); \
    } while (0)

#define DEBUG_TEST_HOOK_WS_GATE_GOODPUT(POOLPTR, CONNS, BPS, OUT_OVERRIDDEN) \
    do { \
        std::function<void(const void*, unsigned, double&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onWsGateGoodputSample; \
        } \
        if (_fn) \
        { \
            _fn((POOLPTR), (CONNS), (BPS)); \
            /* A synthetic goodput signal IS the evidence: the controller's min-event \
               sufficiency checks are bypassed so hook-driven tests stay deterministic \
               and time-bounded (real acks at a test throttle are minutes apart). */ \
            (OUT_OVERRIDDEN) = true; \
        } \
    } while (0)

#define DEBUG_TEST_HOOK_FILEFINGERPRINT_USE_LEGACY_BUGGY_SPARSE_CRC(FLAG) \
    do { \
        std::function<void(bool&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onHookFileFingerprintUseLegacyBuggySparseCrc; \
        } \
        if (_fn) _fn((FLAG)); \
    } while (0)

#define DEBUG_TEST_HOOK_DEVICE_ID(DEVICEID) \
    do { \
        std::function<void(std::string&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onHookDeviceId; \
        } \
        if (_fn) _fn((DEVICEID)); \
    } while (0)

#define DEBUG_TEST_HOOK_HASHCASH_CALCULATION_STARTED \
    do { \
        std::function<void()> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onHashcashCalculationStarted; \
        } \
        if (_fn) _fn(); \
    } while (0)

#define DEBUG_TEST_HOOK_MAC_GENERATION_CHUNK_READ(OFFSET) \
    do { \
        std::function<void(const m_off_t)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onMacGenerationChunkRead; \
        } \
        if (_fn) _fn((OFFSET)); \
    } while (0)

#define DEBUG_TEST_HOOK_FOLDER_UPLOAD_PUTNODES_RESULT(NN) \
    do { \
        std::function<void(std::vector<NewNode>&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onFolderUploadPutnodesResult; \
        } \
        if (_fn) _fn((NN)); \
    } while (0)

#define DEBUG_TEST_HOOK_FOLDER_UPLOAD_SIMULATE_MISSING(FOLDERNAME, MEGANODE, SENT) \
    do { \
        std::function<bool(const std::string&)> _fn; \
        { \
            std::lock_guard<std::mutex> _g(globalMegaTestHooks.mMutex); \
            _fn = globalMegaTestHooks.onFolderUploadSimulateMissing; \
        } \
        if ((SENT) && (MEGANODE) && _fn && _fn((FOLDERNAME))) \
        { \
            (MEGANODE).reset(); \
        } \
    } while (0)
#else
    #define DEBUG_TEST_HOOK_HTTPREQ_POST(x)
    #define DEBUG_TEST_HOOK_RAIDBUFFERMANAGER_SETISRAID(x)
    #define DEBUG_TEST_HOOK_UPLOADCHUNK_FAILED(X)
    #define DEBUG_TEST_HOOK_UPLOADCHUNK_SUCCEEDED(transfer, committer)
    #define DEBUG_TEST_HOOK_DOWNLOAD_FAILED(X)
    #define DEBUG_TEST_HOOK_LIMIT_MAX_REQ_SIZE(X)
    #define DEBUG_TEST_HOOK_NUMBER_OF_CONNECTIONS(connectionsInOutVar, clientNumberOfConnections)
#define DEBUG_TEST_HOOK_ON_PROGRESS_COMPLETED_UPDATE(p)
#define DEBUG_TEST_HOOK_ON_PROGRESS_CONTIGUOUS_UPDATE(p)
#define DEBUG_TEST_HOOK_ON_TRANSFER_REPORT_PROGRESS(p, fp, pb)
#define DEBUG_TEST_HOOK_DOWNLOAD_REQUEST_SINGLEURL(singleUrlFlag)
#define DEBUG_TEST_HOOK_RESET_TRANSFER_LASTACCESSTIME(lastAccessTime)
#define DEBUG_TEST_HOOK_INTERCEPT_LOCKLESS_CS_REQUEST(pendingLocklessCS)
#define DEBUG_TEST_HOOK_INTERCEPT_CS_REQUEST(pendingCS)
#define DEBUG_TEST_HOOK_HTTPREQ_FINISH(HTTPSTATUS, CURLCODE, FAILED)
#define DEBUG_TEST_HOOK_HEARTBEAT_RECEIVED(STATUSCODE, REQID)
#define DEBUG_TEST_HOOK_WS_HANDSHAKE(URL, TIMEOUTMS, ERRSTRING, SHOULDFAIL)
#define DEBUG_TEST_HOOK_WSUPLOAD_SUSTAINED_HANDSHAKE_FAILURE_WINDOW_DS(WINDOWDS)
#define DEBUG_TEST_HOOK_WSUPLOAD_ACKSTALL_TIMEOUT_DS(WINDOWDS)
#define DEBUG_TEST_HOOK_WSUPLOAD_TAILCOMPLETION_TIMEOUT_DS(WINDOWDS)
#define DEBUG_TEST_HOOK_WSUPLOAD_COMPLETION_PAYLOAD_LEN(PAYLEN)
#define DEBUG_TEST_HOOK_WSUPLOAD_FAILURE_DETACHED(REASON, STILL_TRACKED)
#define DEBUG_TEST_HOOK_WSUPLOAD_CORRUPT_TOKEN(FILENO, PAYLOAD, PAYLEN)
#define DEBUG_TEST_HOOK_UPLOAD_PUTNODES_STARTED(TAG)
#define DEBUG_TEST_HOOK_WSUPLOAD_SERVER_EVENT(FILENO, EVENT, CHUNKPOS, RESULT)
#define DEBUG_TEST_HOOK_WS_SEND_FAULT(REMAINING, FORCEDLEN, RESULT)
#define DEBUG_TEST_HOOK_WS_RECV_SWALLOW(OUTBOOL)
#define DEBUG_TEST_HOOK_WSCONN_FORCE_CLOSE_NOW(CONNPTR, POOLPTR, POOLURL, OUTBOOL)
#define DEBUG_TEST_HOOK_WSPOOL_RECONNECT_ATTEMPT(POOLPTR, RETRYCOUNT, FIRSTFAILUREDS)
#define DEBUG_TEST_HOOK_WS_ACKSTALL_FORCE_RECONNECT(CONNPTR, POOLPTR)
#define DEBUG_TEST_HOOK_WS_TAILCOMPLETION_RECOVERY(FILENO, POOLPTR)
#define DEBUG_TEST_HOOK_WSUPLOAD_DROP_COMPLETION(FILENO, DROPPED)
#define DEBUG_TEST_HOOK_WS_SESSION_URL_TRANSITION(TAG, OLDURL, NEWURL, REASON)
#define DEBUG_TEST_HOOK_WS_CHUNK_SEND_OVERQUOTA(TAG, OUT_INJECT_OVERQUOTA)
#define DEBUG_TEST_HOOK_WS_GATE_BP_SAMPLE(POOLPTR, OPEN, BP)
#define DEBUG_TEST_HOOK_WS_GATE_GOODPUT(POOLPTR, CONNS, BPS, OUT_OVERRIDDEN)
#define DEBUG_TEST_HOOK_FILEFINGERPRINT_USE_LEGACY_BUGGY_SPARSE_CRC(FLAG)
#define DEBUG_TEST_HOOK_DEVICE_ID(DEVICEID)
#define DEBUG_TEST_HOOK_HASHCASH_CALCULATION_STARTED
#define DEBUG_TEST_HOOK_MAC_GENERATION_CHUNK_READ(OFFSET)
#define DEBUG_TEST_HOOK_FOLDER_UPLOAD_PUTNODES_RESULT(NN)
#define DEBUG_TEST_HOOK_FOLDER_UPLOAD_SIMULATE_MISSING(FOLDERNAME, MEGANODE, SENT)
#endif

} // namespace

#endif
