/**
 * @file include/mega/transfer/ws/wsupload_engine.h
 * @brief Declaration of `UploadEngine::Impl`, the queue + manager-thread
 *        + pool-coordinator pImpl body backing the public `UploadEngine`
 *        facade declared in `mega/wsupload.h`.
 *
 *        Promoted out of `src/transfer/ws/wsupload.cpp` so that sibling
 *        WS-internal translation units (ws_pool.cpp, ws_conn.cpp,
 *        ws_pool_mgr.cpp) can dereference `mImpl->X` members without
 *        ODR-violating duplicate definitions. The header was later split into
 *        declarations-only + a sibling `src/transfer/ws/wsupload_engine.cpp`
 *        for the non-trivial method bodies. Trivial accessors and the
 *        `withFile<F>` private template helper remain inline here.
 *
 *        SDK-internal architecture header under `include/mega/transfer/ws/`
 *        alongside `ws_encryption.h` and `ws_pool_mgr.h`. Sibling translation
 *        units in `src/transfer/ws/` and `src/megaclient_wsupload.cpp` /
 *        `src/commands_ws.cpp` include it as
 *        `#include "mega/transfer/ws/wsupload_engine.h"`.
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

#ifndef MEGA_TRANSFER_WS_WSUPLOAD_ENGINE_H
#define MEGA_TRANSFER_WS_WSUPLOAD_ENGINE_H 1

#ifdef MEGA_USE_WSUPLOAD

// File-internal types (WsPool, WsConn, WsPoolThread, SteadyTime, ScopedUnlock,
// WSUPLOAD_TRACE, FailReason) plus, via its own includes, mega/wsupload.h
// (UploadEngine, Callbacks) and mega/transfer/ws/ws_pool_mgr.h (WsPoolMgr).
#include "mega/transfer/ws/wsupload_internal.h"

// Full definition of `class WsUploadFile`. Needed because the inline
// `withFile<F>` template body below dereferences WsUploadFile members.
#include "mega/transfer/ws/ws_upload_file.h"

// `mega/megaapp.h` must precede `mega/megaclient.h` (which only forward-declares
// `struct MegaApp* app`) so that sibling TUs see `MegaApp` as a complete type —
// the `invalidatePinnedSessionUrl` body in wsupload_engine.cpp posts a lambda
// that calls `client.app->transfer_update(tp)`. Keeping these includes here in
// the header keeps every TU that includes `wsupload_engine.h` self-contained.
#include "mega/megaapp.h"
#include "mega/megaclient.h" // MegaClient, TransferDbCommitter, wsPostToClientThread
#include "mega/transfer.h" // Transfer, SpeedController

#include <algorithm> // std::max (lossBoostedDatasetConnLimit / lossBoostedGlobalConnCeiling)
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

// Reads the MEGA_WS_LOSS_RECOVERY environment MASTER kill-switch ONCE and returns the
// initial value for UploadEngine::Impl::mLossRecovery (default ON; "0" disables). The four
// sub-knob readers below each read their own env var the same way (default ON, only "0"
// disables); their effective Impl-field values are ANDed with this master so
// MEGA_WS_LOSS_RECOVERY=0 forces every sub-knob OFF in one flip. Defined in
// src/transfer/ws/wsupload_engine.cpp so this header does not pull in mega/utils.h (the
// cross-platform Utils::getenv lives there). Free functions rather than inline ctor bodies
// so the heavy include stays out of every TU that includes this header.
bool wsLossRecoveryEnvDefault();
bool wsAdaptiveHandshakeEnvDefault(); // MEGA_WS_ADAPTIVE_HANDSHAKE -> mAdaptiveHandshake
bool wsResendDedupEnvDefault(); // MEGA_WS_RESEND_DEDUP -> mResendDedupCancel
bool wsAckedRewindEnvDefault(); // MEGA_WS_ACKED_REWIND -> mAckedChunkRewind
bool wsRefreshThrottleEnvDefault(); // MEGA_WS_REFRESH_THROTTLE -> mDistressRefreshThrottle
bool wsParallelHandshakeEnvDefault(); // MEGA_WS_PARALLEL_HANDSHAKE -> mParallelHandshake
bool wsLossConnBumpEnvDefault(); // MEGA_WS_LOSS_CONN_BUMP -> mLossConnBump
bool wsDatasetConnBumpEnvDefault(); // MEGA_WS_DATASET_CONN_BUMP -> mDatasetConnBump
bool wsSingleFileConnBumpEnvDefault(); // MEGA_WS_SINGLEFILE_CONN_BUMP -> mSingleFileConnBump (DEFAULT OFF)
unsigned char wsDatasetConnLimitOverrideEnvDefault(); // MEGA_WS_DATASET_CONN_LIMIT -> mDatasetConnLimitOverride (0=use constant)
bool wsDatasetConnGateEnvDefault(); // MEGA_WS_DATASET_CONN_GATE -> mDatasetConnGate (ANDed with mLossRecovery)
unsigned wsGateWindowMsEnvDefault(); // MEGA_WS_GATE_WINDOW_MS -> mGateWindowMs (default 1000)
unsigned wsGateGainPctEnvDefault(); // MEGA_WS_GATE_GAIN_PCT -> mGateGainPct (default 5)
unsigned char wsGateStepEnvDefault(); // MEGA_WS_GATE_STEP -> mGateStep (default 1)
// QCT-K gate v2 tunables (SDK-5360 fu8 S8). Same read-once function-local-static pattern; all
// clamp to sane ranges in the parser. Inert when the gate is off (controller never runs).
unsigned wsGateBpQuorumPctEnvDefault(); // MEGA_WS_GATE_BP_QUORUM_PCT -> mGateBpQuorumPct (default 50)
unsigned wsGateEngageWindowsEnvDefault(); // MEGA_WS_GATE_ENGAGE_WINDOWS -> mGateEngageWindows (default 2)
unsigned wsGateProbeWindowsEnvDefault(); // MEGA_WS_GATE_PROBE_WINDOWS -> mGateProbeWindows (default 2)
unsigned wsGateRetreatMaxWindowsEnvDefault(); // MEGA_WS_GATE_RETREAT_MAX_WINDOWS -> mGateRetreatMaxWindows (default 300)
unsigned wsGateTrimWindowsEnvDefault(); // MEGA_WS_GATE_TRIM_WINDOWS -> mGateTrimWindows (default 10; 0=off)
unsigned wsGateMinEventsEnvDefault(); // MEGA_WS_GATE_MIN_EVENTS -> mGateMinEvents (default 8, clamp [1,64])
unsigned char wsGateCeilingMultEnvDefault(); // MEGA_WS_GATE_CEILING_MULT -> mGateCeilingMult (default 4; 0=off)
unsigned wsConnTelemetryMsEnvDefault(); // MEGA_WS_CONN_TELEMETRY_MS -> mConnTelemetryMs (default 10000; 0=off)
dstime wsAckStallTimeoutDsEnvDefault(); // MEGA_WS_ACKSTALL_TIMEOUT_MS (ms->ds) -> mAckStallTimeoutDsOverride (0=use ACKSTALLTIMEOUT)
bool wsAckStallWatchdogEnvDefault(); // MEGA_WS_ACKSTALL_WATCHDOG -> mAckStallWatchdog (ANDed with mLossRecovery)
bool wsTailCompletionWatchdogEnvDefault(); // MEGA_WS_TAILCOMPLETION_WATCHDOG -> mTailCompletionWatchdog (narrows only)
long wsHandshakeTimeoutMsEnvDefault(); // MEGA_WS_HANDSHAKE_TIMEOUT_MS -> mHandshakeTimeoutMsOverride (0=use constants)
dstime wsHandshakeFailWindowDsEnvDefault(); // MEGA_WS_HANDSHAKE_FAIL_WINDOW_MS (ms->ds) -> mHandshakeFailWindowDsOverride (0=use HANDSHAKEFAILTIMEOUT)
dstime wsTailCompletionTimeoutDsEnvDefault(); // MEGA_WS_TAIL_COMPLETION_TIMEOUT_MS (ms->ds) -> mTailCompletionTimeoutDsOverride (0=use TAILCOMPLETIONTIMEOUT)
// Small-file cold-start (candidate 3c). INDEPENDENT of the MEGA_WS_LOSS_RECOVERY master:
// it is a small-file overhead concern, not loss-recovery, so it is NOT ANDed with
// mLossRecovery in the ctor. Default ON; only an explicit "0" disables.
bool wsSmallFileColdStartEnvDefault(); // MEGA_WS_SMALLFILE_COLDSTART -> mSmallFileColdStart

// ========== UploadEngine::Impl (queue + mgr + thread) ==========
//
// All non-trivial method bodies live in src/transfer/ws/wsupload_engine.cpp.
// This header keeps:
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
        // Loss-recovery feature flags. Read the env knobs once at construction so the
        // loss20cap bench is A/B-able in one binary. mLossRecovery is the MASTER kill
        // (MEGA_WS_LOSS_RECOVERY=0 reproduces pre-fix behavior); each sub-knob below is
        // ANDed with it so the master forces all sub-knobs OFF in one flip. All default ON.
        mLossRecovery = wsLossRecoveryEnvDefault();
        mAdaptiveHandshake = wsAdaptiveHandshakeEnvDefault() && mLossRecovery;
        mResendDedupCancel = wsResendDedupEnvDefault() && mLossRecovery;
        mAckedChunkRewind = wsAckedRewindEnvDefault() && mLossRecovery;
        mDistressRefreshThrottle = wsRefreshThrottleEnvDefault() && mLossRecovery;
        mParallelHandshake = wsParallelHandshakeEnvDefault() && mLossRecovery;
        mLossConnBump = wsLossConnBumpEnvDefault() && mLossRecovery;
        mDatasetConnBump = wsDatasetConnBumpEnvDefault() && mLossRecovery;
        // Single-file connection ramp (S7 Lever C, env MEGA_WS_SINGLEFILE_CONN_BUMP, DEFAULT
        // OFF). ANDed with mLossRecovery like mDatasetConnBump so the master kill forces it off.
        mSingleFileConnBump = wsSingleFileConnBumpEnvDefault() && mLossRecovery;
        mDatasetConnLimitOverride = wsDatasetConnLimitOverrideEnvDefault();
        // Goodput-saturation gate (SDK-5360). ANDed with mLossRecovery, mirroring
        // mDatasetConnBump, so MEGA_WS_LOSS_RECOVERY=0 forces the legacy path. When OFF, the
        // dataset bump reverts to today's unconditional jump-to-K. The numeric tunables are
        // const-after-init (not ANDed): inert when the gate is off (controller never runs).
        mDatasetConnGate = wsDatasetConnGateEnvDefault() && mLossRecovery;
        mGateWindowMs = wsGateWindowMsEnvDefault();
        mGateGainPct = wsGateGainPctEnvDefault();
        mGateStep = wsGateStepEnvDefault();
        // QCT-K gate v2 tunables (const-after-init; inert when the gate is off).
        mGateBpQuorumPct = wsGateBpQuorumPctEnvDefault();
        mGateEngageWindows = wsGateEngageWindowsEnvDefault();
        mGateProbeWindows = wsGateProbeWindowsEnvDefault();
        mGateRetreatMaxWindows = wsGateRetreatMaxWindowsEnvDefault();
        mGateTrimWindows = wsGateTrimWindowsEnvDefault();
        mGateMinEvents = wsGateMinEventsEnvDefault();
        mGateCeilingMult = wsGateCeilingMultEnvDefault();
        mConnTelemetryMs = wsConnTelemetryMsEnvDefault();
        // Ack-stall watchdog (fu8 S6). ANDed with mLossRecovery so the master kill reproduces
        // full pre-fix behavior; independently toggleable via MEGA_WS_ACKSTALL_WATCHDOG for the
        // Goal-2d watchdog-off vs -on A/B. Timeout override is const-after-init.
        mAckStallWatchdog = wsAckStallWatchdogEnvDefault() && mLossRecovery;
        mTailCompletionWatchdog = wsTailCompletionWatchdogEnvDefault();
        mAckStallTimeoutDsOverride = wsAckStallTimeoutDsEnvDefault();
        // Handshake timeout + coupled fail-window (S7 Lever A). Pure numeric overrides (default
        // 0 = use the compile-time kHandshakeTimeout* constants / HANDSHAKEFAILTIMEOUT), so NOT
        // ANDed with mLossRecovery. Const-after-init; consumed via handshakeTimeoutMs() /
        // handshakeFailWindowDs(). Kept coupled: MEGA_WS_HANDSHAKE_TIMEOUT_MS must stay under
        // MEGA_WS_HANDSHAKE_FAIL_WINDOW_MS or one attempt blows the whole failure budget.
        mHandshakeTimeoutMsOverride = wsHandshakeTimeoutMsEnvDefault();
        mHandshakeFailWindowDsOverride = wsHandshakeFailWindowDsEnvDefault();
        mTailCompletionTimeoutDsOverride = wsTailCompletionTimeoutDsEnvDefault();
        // Small-file cold-start (candidate 3c). INDEPENDENT of mLossRecovery (small-file
        // concern, not loss-recovery), so it is NOT ANDed with the master kill-switch.
        mSmallFileColdStart = wsSmallFileColdStartEnvDefault();
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

    // Signatures always-compile so tests/integration/wsupload/headers/WsUploadDebugHelpers.h
    // can include this header in hooks-OFF builds. Bodies internally gate via
    // MEGASDK_DEBUG_TEST_HOOKS_ENABLED — in hooks-OFF they return a default-constructed
    // out with `found=false` because the NDEBUG-only internal counters they would
    // otherwise read are not compiled in.
    bool getPoolStateForTesting(const std::string& url,
                                UploadEngine::PoolStateForTesting& out) const;

    bool getWsUploadStatsForTesting(UploadEngine::WsUploadStatsForTesting& out) const;

    // Release-safe bench-framework hook — see UploadEngine::getAndResetBenchThrottleStats.
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

    // Effective loss-gated DATASET boosted connection limit: the runtime numeric override
    // (MEGA_WS_DATASET_CONN_LIMIT) when set, else the compile-time constant. Consumed by
    // WsPool::lossBoostedConnLimitLocked's dataset branch. Never below the default pool limit.
    unsigned char lossBoostedDatasetConnLimit() const
    {
        const unsigned char k = mDatasetConnLimitOverride
                                    ? mDatasetConnLimitOverride
                                    : WsPool::kLossBoostedDatasetConnLimit;
        return std::max<unsigned char>(k, mPoolConnectionLimit);
    }

    // Cross-pool concurrency ceiling for dataset-boosted pools. Normally the constant, but an
    // override above it lifts the ceiling to fit (so a K=32/36 proof-bench arm is measurable
    // on a single active pool). Applied in WsPoolMgr::checkPools over live per-pool conn counts.
    unsigned char lossBoostedGlobalConnCeiling() const
    {
        return std::max<unsigned char>(WsPool::kLossBoostedGlobalConnCeiling,
                                       lossBoostedDatasetConnLimit());
    }

    // Goodput-saturation gate sampling window in deciseconds (MEGA_WS_GATE_WINDOW_MS / 100,
    // floored at 1ds so a tiny positive value still arms). Consumed by the WsPoolMgr::checkPools
    // ramp controller (WsPool::runGoodputGateLocked).
    dstime gateWindowDs() const
    {
        const dstime ds = msToDs(static_cast<std::int64_t>(mGateWindowMs));
        return ds > 0 ? ds : 1;
    }

    // [WsConnTelemetry] pacing period in deciseconds (MEGA_WS_CONN_TELEMETRY_MS / 100, floored at
    // 1ds). Caller checks mConnTelemetryMs != 0 to honour 0=off before using this. Consumed by
    // WsPoolMgr::checkPools to emit the per-pool conn-trajectory line on BOTH gate states.
    dstime connTelemetryDs() const
    {
        const dstime ds = msToDs(static_cast<std::int64_t>(mConnTelemetryMs));
        return ds > 0 ? ds : 1;
    }

    // Effective ack-stall watchdog window (fu8 S6): the runtime override
    // (MEGA_WS_ACKSTALL_TIMEOUT_MS, already converted to ds) when set, else the compile-time
    // WsPool::ACKSTALLTIMEOUT. Consumed by WsPoolMgr::checkPools.
    dstime ackStallTimeoutDs() const
    {
        return mAckStallTimeoutDsOverride ? mAckStallTimeoutDsOverride
                                          : WsPool::ACKSTALLTIMEOUT;
    }

    // ---- SDK-5360 fu8 Session 7, Lever A: env-tunable handshake timeout + coupled fail-window.
    // Per-attempt WS TLS+upgrade handshake timeout (ms), selected in WsConn::connectWS. Moved
    // here from ws_conn.cpp's anonymous namespace so the MEGA_WS_HANDSHAKE_TIMEOUT_MS override
    // can select through one accessor (leaving them file-local + unused would risk
    // -Wunused-const-variable on clang). kHandshakeTimeoutLossMs (loss-adaptive path,
    // mAdaptiveHandshake ON): 45s gives a loss-throttled upgrade enough wall-clock to complete
    // while staying UNDER the coupled HANDSHAKEFAILTIMEOUT (60s) escalation gate so a dead
    // endpoint still surfaces onFail within the failure budget. kHandshakeTimeoutMs (baseline,
    // adaptive OFF): the legacy 15s. A clean handshake completes in <1s either way, so clean-
    // network behaviour is byte-identical. The per-attempt timeout MUST stay < the fail-window
    // (handshakeFailWindowDs) or one attempt blows the whole failure budget.
    static constexpr long kHandshakeTimeoutMs{15000};
    static constexpr long kHandshakeTimeoutLossMs{45000};

    // Effective per-attempt handshake timeout (ms): the runtime override
    // (MEGA_WS_HANDSHAKE_TIMEOUT_MS) when set, else the loss/baseline constant chosen by the
    // adaptive flag. Consumed by WsConn::connectWS (both the parallel + baton paths).
    long handshakeTimeoutMs(bool adaptive) const
    {
        if (mHandshakeTimeoutMsOverride > 0)
            return mHandshakeTimeoutMsOverride;
        return adaptive ? kHandshakeTimeoutLossMs : kHandshakeTimeoutMs;
    }

    // Effective sustained-handshake-failure escalation window (ds): the runtime override
    // (MEGA_WS_HANDSHAKE_FAIL_WINDOW_MS, already converted to ds) when set, else the compile-
    // time WsPool::HANDSHAKEFAILTIMEOUT (60s). Consumed by WsPool::poolWorkerThread. Coupled to
    // handshakeTimeoutMs(): the per-attempt timeout must stay under this window.
    dstime handshakeFailWindowDs() const
    {
        return mHandshakeFailWindowDsOverride > 0 ? mHandshakeFailWindowDsOverride
                                                  : WsPool::HANDSHAKEFAILTIMEOUT;
    }

    // Effective tail-completion watchdog window (fu8 S9): the runtime override
    // (MEGA_WS_TAIL_COMPLETION_TIMEOUT_MS, already converted to ds) when set, else the
    // compile-time WsPool::TAILCOMPLETIONTIMEOUT (60s). Consumed by WsPoolMgr::checkPools.
    dstime tailCompletionTimeoutDs() const
    {
        return mTailCompletionTimeoutDsOverride > 0 ? mTailCompletionTimeoutDsOverride
                                                    : WsPool::TAILCOMPLETIONTIMEOUT;
    }

    void setMaxConnections(const unsigned char maxConnections);

    // Must be called with uploadMutex held.
    // requesterKey (S12 Cluster-B fix v2): identity token for head-waiter fairness —
    // callers pass their pool pointer (never dereferenced). All-or-nothing grants with
    // phase-locked pollers otherwise let one pool win the accrual crossing EVERY cycle
    // and starve an equal-need sibling forever (deterministic repro:
    // RepeatedPauseResumeMixedPoolsStress capped arm). The first consumer to fail
    // registers as the head waiter; its need is RESERVED out of the budget for other
    // keys until served (surplus above the reservation stays grantable — work-
    // conserving), with staleness expiry for waiters that stop asking.
    bool consumeUploadBudget(const m_off_t bytes,
                             dstime* retryAfterDs = nullptr,
                             const void* requesterKey = nullptr);
    // S12 Cluster-B fix: refund a chunk whose send was cancelled AFTER budgeting (the
    // paused-underneath requeue in sendChunk) so pool-mates are not charged for bytes
    // that never reached the wire. Caller holds uploadMutex (consumeUploadBudget's
    // contract); clamped to the same burst cap.
    void refundUploadBudget(const m_off_t bytes);

    void setMaxUploadSpeed(const m_off_t bytesPerSecond);

    // Called by pools to pick next file that matches [min,max)
    WsUploadFile* nextEligible(const m_off_t min,
                               const m_off_t max,
                               const std::string* requiredSessionUrl,
                               const WsPool* requestingPool);

    // fu7-21 Lever F: promoted out of #ifndef NDEBUG so poolWorkerThread can use
    // it in Release to gate lazy-connect of idle size-class pools. Side-effect-free
    // read-only scan of fileList (unlike nextEligible, which advances the cursor).
    bool hasEligibleFileForPool(const m_off_t min,
                                const m_off_t max,
                                const std::string* requiredSessionUrl,
                                const WsPool* requestingPool) const;

    // Candidate 3c (small-file cold-start). Counts eligible files for [min,max) on
    // `requestingPool`, stopping the scan at the `cap`-th hit (so a huge queue cannot
    // make this O(N) on the cold-start gate hot path). Same eligibility predicate as
    // hasEligibleFileForPool. When the return value is exactly 1 and `firstEligibleSize`
    // is non-null, *firstEligibleSize is set to that single file's size() (the ACTUAL
    // file size, used by WsPool::coldStartHandshakeCapLocked to apply the small-file
    // ceiling -- NOT the size-class ceiling mMaxFileSize). Side-effect-free, read-only;
    // caller must hold uploadMutex. Returns min(eligibleCount, cap).
    unsigned eligibleFileCountForPoolCappedLocked(const m_off_t min,
                                                  const m_off_t max,
                                                  const std::string* requiredSessionUrl,
                                                  const WsPool* requestingPool,
                                                  const unsigned cap,
                                                  m_off_t* firstEligibleSize) const;

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
    // S15 FIX-T regression counter: worker-loop iterations executed while stopping()
    // was already true. Bounded <=1 per worker lifetime by the loop-exit condition;
    // explodes if that condition regresses (see poolWorkerThread + the
    // DEBUG_TEST_HOOK_WS_TEARDOWN_WORKER_CHURN seam).
    std::atomic<std::uint64_t> mWorkerItersWhileStopping{0};

    dstime currentTime{0};
    std::atomic<std::uint32_t> nextFileNo{1};
    unsigned char mPoolConnectionLimit{3};
    m_off_t mMaxUploadSpeed{0};
    m_off_t mUploadBudget{0};
    dstime mUploadBudgetLastDs{0};
    // S12 Cluster-B fix v3: FIFO budget-waiter queue (see consumeUploadBudget). Keys are
    // identity tokens only — NEVER dereferenced (safe across pool retirement; staleness
    // expiry bounds any recycled-address confusion window). Strict serve order: under
    // contention every asker is served in registration order, bounding starvation at
    // (queue length × per-grant accrual time). A v2-style single reservation was
    // falsified: the reserved waiter monopolizes every accrual crossing and inverts the
    // starvation (JENKINS_RCA_S12 Cluster B).
    struct BudgetWaiter
    {
        const void* key{nullptr};
        m_off_t bytes{0};
        dstime lastAskDs{0};
    };
    std::vector<BudgetWaiter> mBudgetWaiters;
    // S12 Cluster-E fix: files whose IO holder never quiesced within the bounded
    // removal wait (blocked FS syscall — e.g. a dying FUSE-backed source path at
    // locallogout). Kept alive so the stuck reader's member accesses stay valid;
    // guarded by uploadMutex; freed with the engine (or leaked with it if the engine
    // itself cannot quiesce — see MegaClient::wsLocallogoutCleanup).
    std::vector<std::unique_ptr<WsUploadFile>> mAbandonedFiles;
    // Live pool-worker count for the bounded locallogout quiesce (entry/exit in
    // WsPool::poolWorkerThread).
    std::atomic<int> mLiveWorkerThreads{0};
    // S13 round-3 (Cluster I+J / SDK-6298 FU1 §4.1): manager-thread liveness for the
    // same quiesce. workersQuiesced() previously implied only pool-worker exit, so
    // ~Impl's uploadThread.join() could hang unbounded and UNLOGGED when the manager
    // was wedged — the "Logout failed after 600 seconds" CI job-killers. Set/cleared
    // by UploadEngine::Impl::run() itself (true at body entry, false as it returns).
    std::atomic<bool> mManagerThreadLive{false};
    bool paused{false};
    // Loss-recovery feature flags. Each is assigned EXACTLY ONCE in the ctor (before any
    // worker/manager thread exists) and never mutated thereafter, so it is effectively
    // const-after-init: reading it on any thread (including connectWS with uploadMutex
    // released, or the manager-thread refreshPools()) is race-free. When the master is
    // false (or a sub-knob is false) the corresponding path is byte-identical to pre-fix.
    //
    // mLossRecovery is the MASTER (env MEGA_WS_LOSS_RECOVERY, default ON). The four sub-
    // knobs are ANDed with it in the ctor, so MEGA_WS_LOSS_RECOVERY=0 forces them all OFF.
    bool mLossRecovery{true};
    // mAdaptiveHandshake (env MEGA_WS_ADAPTIVE_HANDSHAKE, default ON): gates the loss-
    // adaptive handshake timeout (WsConn::connectWS, ws_conn.cpp).
    bool mAdaptiveHandshake{true};
    // mResendDedupCancel (env MEGA_WS_RESEND_DEDUP, default ON): gates the opcode-2
    // (AlreadyOnServer) queued-resend purge (WsConn::onmessage, ws_conn.cpp).
    bool mResendDedupCancel{true};
    // mAckedChunkRewind (env MEGA_WS_ACKED_REWIND, default ON): gates the whole-chunk-
    // boundary acked-range rewind (WsConn::onmessage markRangeAcked +
    // WsPool::retryChunksOnTheWireLocked skip-already-acked).
    bool mAckedChunkRewind{true};
    // mDistressRefreshThrottle (env MEGA_WS_REFRESH_THROTTLE, default ON): gates the
    // refresh-rate throttle in WsPoolMgr::refreshPools() (ws_pool_mgr.cpp).
    bool mDistressRefreshThrottle{true};
    // mParallelHandshake (env MEGA_WS_PARALLEL_HANDSHAKE, default ON): gates Design A — running
    // each WS connection's TLS+upgrade handshake directly on its OWN worker thread
    // (WsConn::connectWS, ws_conn.cpp) instead of serially on the client thread via the Baton +
    // wsPostToClientThread hop. ANDed with mLossRecovery so MEGA_WS_LOSS_RECOVERY=0 forces the
    // legacy serial path. When false connectWS() is byte-identical to the pre-Design-A Baton
    // path. Requires the curlsh lock callbacks (net.cpp) for the now-concurrent DNS/SSL-session
    // cache writes.
    bool mParallelHandshake{true};
    // mLossConnBump (env MEGA_WS_LOSS_CONN_BUMP, default ON): gates the loss-gated
    // connection-count bump in WsPool::lossBoostedConnLimitLocked() (ws_pool.cpp), wired
    // into the WsPoolMgr::checkPools scale-up. When a lone small file has observed loss
    // its connection target is widened toward kLossBoostedConnLimit (8) so it uploads on
    // ~8 flows like develop does under loss. ANDed with mLossRecovery so
    // MEGA_WS_LOSS_RECOVERY=0 forces the legacy (no-boost) path. When false
    // lossBoostedConnLimitLocked() always returns the default poolConnectionLimit() and
    // the scale-up is byte-identical to the pre-bump behaviour.
    bool mLossConnBump{true};
    // mDatasetConnBump (env MEGA_WS_DATASET_CONN_BUMP, default ON): gates the loss-gated
    // DATASET connection-count bump — the complement of mLossConnBump — in
    // WsPool::lossBoostedConnLimitLocked() (ws_pool.cpp). When a DATASET pool (eligibleCount>=2,
    // or a lone file above the small-file ceiling) has observed loss, its connection target is
    // widened toward lossBoostedDatasetConnLimit() so the dataset uploads on more independent
    // TCP flows (root cause = FLOW count). ANDed with mLossRecovery. Decoupled from mLossConnBump
    // at the runtime gate (either bump toggles independently). When false the dataset branch is
    // unreachable and scale-up is byte-identical to the pre-bump behaviour.
    bool mDatasetConnBump{true};
    // mSingleFileConnBump (env MEGA_WS_SINGLEFILE_CONN_BUMP, DEFAULT OFF): S7 Lever C. When on,
    // WsPool::lossBoostedConnLimitLocked lets a single-file pool (mNumPoolFiles==1) reach the
    // dataset-boosted ceiling instead of requiring >=2 files, then the goodput gate ramps it
    // (withholding the extra conns on a clean link -- one file's chunks split across pool conns
    // via the shared mUploadingFile head cursor in nextChunk, so the extra flows are usable).
    // ANDed with mLossRecovery like mDatasetConnBump so MEGA_WS_LOSS_RECOVERY=0 also forces it
    // off. When off, byte-identical to today (the dataset branch still needs mNumPoolFiles>=2).
    bool mSingleFileConnBump{false};
    // mDatasetConnLimitOverride (env MEGA_WS_DATASET_CONN_LIMIT, default 0 = use the constant
    // kLossBoostedDatasetConnLimit=32): runtime numeric override letting the Queue-B proof bench
    // sweep K (24/32/36) on ONE binary. Const-after-init (assigned once in the ctor). Consumed
    // via lossBoostedDatasetConnLimit(); a value above the ceiling lifts it via
    // lossBoostedGlobalConnCeiling().
    unsigned char mDatasetConnLimitOverride{0};
    // mDatasetConnGate (env MEGA_WS_DATASET_CONN_GATE, default ON): gates the goodput-
    // saturation GATE in WsPoolMgr::checkPools / WsPool::runGoodputGateLocked (ws_pool.cpp).
    // When ON, a dataset-boosted pool RAMPS toward the dataset limit K only while aggregate
    // server-confirmed goodput keeps rising under full send-buffer backpressure, and actively
    // steps back down when an added flow does not help -- so clean links (high- AND low-BW)
    // stay at the low default and only loss-limited datasets widen. ANDed with mLossRecovery.
    // When false, checkPools reverts to the pre-gate UNCONDITIONAL jump straight to K (today's
    // behaviour), so gate-vs-unconditional is A/B-toggleable in one binary. Const-after-init.
    bool mDatasetConnGate{true};
    // mGateWindowMs (env MEGA_WS_GATE_WINDOW_MS, default 1000): goodput-gate sampling window in
    // milliseconds. The controller samples/decides at most once per window (NOT every checkPools
    // tick). Const-after-init; consumed as deciseconds via gateWindowDs().
    unsigned mGateWindowMs{1000};
    // mGateGainPct (env MEGA_WS_GATE_GAIN_PCT, default 5): the minimum percentage rise in
    // aggregate goodput a +step probe must produce to be kept (else it is retreated). This is
    // the clean-vs-loss discriminator. Const-after-init.
    unsigned mGateGainPct{5};
    // mGateStep (env MEGA_WS_GATE_STEP, default 1): connections added/removed per ramp probe
    // (a pure +1/-1 hill-climb by default). Const-after-init.
    unsigned char mGateStep{1};
    // ---- QCT-K gate v2 tunables (SDK-5360 fu8 S8). All const-after-init, NOT ANDed with
    // mLossRecovery (they are numeric tunables inert when the gate is off). Consumed by
    // WsPool::runGoodputGateLocked. See docs/../BENCHMARKS.md for the knob table + the "≈gate-v1"
    // bisection recipe.
    // mGateBpQuorumPct (env MEGA_WS_GATE_BP_QUORUM_PCT, default 50, clamp [1,100]): the % of OPEN
    // conns that must be backpressured at a ~2 Hz tick for that tick to count as quorum-true.
    // 100 approximates the S7 all-conns predicate; floor 1 (0 refused — it would let MassNotify
    // engage).
    unsigned mGateBpQuorumPct{50};
    // mGateEngageWindows (env MEGA_WS_GATE_ENGAGE_WINDOWS, default 2, clamp [1,60]): consecutive
    // quorum-true windows required to engage the first widen from base (anti-stampede debounce).
    unsigned mGateEngageWindows{2};
    // mGateProbeWindows (env MEGA_WS_GATE_PROBE_WINDOWS, default 2, clamp [1,10]): the probe
    // measurement horizon (goodput averaged over it; 1 restores S7 single-window judging).
    unsigned mGateProbeWindows{2};
    // mGateRetreatMaxWindows (env MEGA_WS_GATE_RETREAT_MAX_WINDOWS, default 300, clamp [5,3000]):
    // cap for the exponential failed-probe backoff (base 5 windows == S7's retreat cooldown).
    unsigned mGateRetreatMaxWindows{300};
    // mGateTrimWindows (env MEGA_WS_GATE_TRIM_WINDOWS, default 10, clamp [0,255]): consecutive
    // quorum-false windows before each halving trim toward base; 0 = trim off (S7 never-shrink).
    // Capped at 255 (not the table's 600) because the debounce counter mGateNoBpWindows is a
    // uint8_t — 255 windows is far beyond the 10-window default. See BENCHMARKS.md.
    unsigned mGateTrimWindows{10};
    // mGateMinEvents (env MEGA_WS_GATE_MIN_EVENTS, default 8, clamp [1,64], amendment A1):
    // minimum server-confirm EVENTS a probe horizon must contain before the gain judge rules
    // (auto-extends the horizon on slow links, hard-capped at 10 windows -> insufficient
    // evidence FAILS the probe). Kills the lumpy-ack noise that chain-confirmed probes against
    // zero baselines on capped links (S8 cleanNet8m smoke: climb to 28 on 0-loss).
    unsigned mGateMinEvents{8};
    // mGateCeilingMult (env MEGA_WS_GATE_CEILING_MULT, default 4, clamp [0,32]): effective ceiling
    // = min(ceilingIn, base*mult), applied IN-GATE so the GATE=0 jump-to-K path stays byte-
    // identical on every platform. Desktop 8*4=32 (no change); mobile 3*4=12. 0 = disable (use
    // ceilingIn as-is). An explicit MEGA_WS_DATASET_CONN_LIMIT does NOT bypass this (orthogonal).
    unsigned char mGateCeilingMult{4};
    // mConnTelemetryMs (env MEGA_WS_CONN_TELEMETRY_MS, default 10000, 0=off): period of the
    // [WsConnTelemetry] per-pool conn-trajectory line emitted in WsPoolMgr::checkPools on BOTH
    // gate states (so the GATE=0/K32 A/B arm and guardrail-killed runs are finally scorable).
    unsigned mConnTelemetryMs{10000};
    // mAckStallWatchdog (env MEGA_WS_ACKSTALL_WATCHDOG, default ON): gates the ack-stall
    // watchdog in WsPoolMgr::checkPools that force-reconnects a silently-hung OPEN conn
    // (SDK-5360 fu8 Session 6 — server acks stale past ACKSTALLTIMEOUT while chunks in-flight).
    // ANDed with mLossRecovery; independently toggleable for the Goal-2d watchdog A/B.
    // Const-after-init (assigned once in the ctor before any worker thread exists).
    bool mAckStallWatchdog{true};
    // mTailCompletionWatchdog (env MEGA_WS_TAILCOMPLETION_WATCHDOG, default ON): independent
    // enable for the tail-completion watchdog (fu8 S11; previously only the shared
    // mAckStallWatchdog gate existed, so the tail net could not be A/B'd or killed alone).
    // ANDed with mAckStallWatchdog at the checkPools gate — it only ever narrows, so the
    // watchdog-off A/B arms stay byte-identical. Const-after-init.
    bool mTailCompletionWatchdog{true};
    // mAckStallTimeoutDsOverride (env MEGA_WS_ACKSTALL_TIMEOUT_MS, default 0 = use the constant
    // WsPool::ACKSTALLTIMEOUT=45s): runtime numeric override (deciseconds) letting the Goal-2d
    // bench sweep the window on ONE binary. Const-after-init; consumed via ackStallTimeoutDs().
    dstime mAckStallTimeoutDsOverride{0};
    // mHandshakeTimeoutMsOverride (env MEGA_WS_HANDSHAKE_TIMEOUT_MS, default 0 = use the compile-
    // time kHandshakeTimeoutMs/kHandshakeTimeoutLossMs): S7 Lever A runtime override
    // (milliseconds) for the per-attempt WS handshake timeout selected in WsConn::connectWS.
    // Const-after-init; consumed via handshakeTimeoutMs(). NOT ANDed with mLossRecovery.
    long mHandshakeTimeoutMsOverride{0};
    // mHandshakeFailWindowDsOverride (env MEGA_WS_HANDSHAKE_FAIL_WINDOW_MS, default 0 = use
    // WsPool::HANDSHAKEFAILTIMEOUT=60s): S7 Lever A runtime override (deciseconds; the env is ms)
    // for the sustained-handshake-failure escalation window in WsPool::poolWorkerThread. Coupled
    // to mHandshakeTimeoutMsOverride (the per-attempt timeout must stay under the window). Const-
    // after-init; consumed via handshakeFailWindowDs(). NOT ANDed with mLossRecovery.
    dstime mHandshakeFailWindowDsOverride{0};
    // mTailCompletionTimeoutDsOverride (env MEGA_WS_TAIL_COMPLETION_TIMEOUT_MS, default 0 = use
    // WsPool::TAILCOMPLETIONTIMEOUT=60s): fu8 S9 runtime override (deciseconds; the env is ms)
    // for the tail-completion watchdog window in WsPoolMgr::checkPools. Const-after-init;
    // consumed via tailCompletionTimeoutDs(). NOT ANDed with mLossRecovery (a lost completion
    // frame is a correctness wedge, not a loss-recovery optimisation).
    dstime mTailCompletionTimeoutDsOverride{0};
    // mSmallFileColdStart (env MEGA_WS_SMALLFILE_COLDSTART, default ON): gates candidate
    // 3c (small-file cold-start cap=1) read in WsPool::coldStartHandshakeCapLocked()
    // (ws_pool.cpp). Same const-after-init / race-free discipline as the four flags above
    // (assigned exactly once in the ctor before any thread exists). DELIBERATELY NOT ANDed
    // with mLossRecovery: it is a clean-network single-small-file overhead optimisation,
    // orthogonal to the loss-recovery master kill-switch. When false the cold-start cap is
    // byte-identical to fix #2 (always COLDSTART_HANDSHAKE_CONNS).
    bool mSmallFileColdStart{true};
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
