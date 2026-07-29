/**
 * @file src/transfer/ws/wsupload_engine.cpp
 * @brief Non-trivial `UploadEngine::Impl` member bodies — split out of
 *        include/mega/transfer/ws/wsupload_engine.h so the header keeps to
 *        declarations + the inline-required pieces (ctor, trivial accessors,
 *        the `withFile<F>` private template, and the one-line iterator helpers).
 *
 *        Bodies hosted here (lifecycle / queue / pause / state / pools):
 *
 *          - Lifecycle: ~Impl, stop, start, kick, run.
 *          - Queue mutation: enqueue, reposition, remove, eraseFromFileListLocked.
 *          - Per-file commands: pause, unpause, setRetryUntil, markFailed.
 *          - Per-file queries: isUploading (Transfer&) / isUploading (const Transfer&),
 *            getTransferStats, drainConfirmedChunkMacs, getSessionUrl.
 *          - Test-only queries (always-compile signatures, gated bodies):
 *            getPoolStateForTesting, getWsUploadStatsForTesting,
 *            isTrackedForTesting, getFilePoolIdForTesting.
 *          - URL invalidation: invalidatePinnedSessionUrl.
 *          - Notifications / connection limits: notifyWorkers,
 *            notifyNetworkDisconnect, setMaxConnections, setMaxUploadSpeed,
 *            consumeUploadBudget.
 *          - Scheduling: nextEligible, hasEligibleFileForPoolForTesting (NDEBUG-only).
 *          - Worker bookkeeping: cleanupExitedPoolThreads, bumpQueueVersion.
 *
 *        Couplings (per coupling-map §5 SS-1):
 *          - UploadEngine::Impl ↔ WsPoolMgr (DEEP): start/stop/kick/run/
 *            invalidatePinnedSessionUrl/setMaxConnections/cleanupExitedPoolThreads/
 *            bumpQueueVersion all iterate poolMgr.mPools.
 *          - UploadEngine::Impl ↔ WsPool (OK): same iteration; full struct
 *            visible via wsupload_internal.h.
 *          - UploadEngine::Impl ↔ WsUploadFile (OK): enqueue/remove/withFile/
 *            invalidatePinnedSessionUrl/nextEligible dereference file members;
 *            full class visible via ws_upload_file.h.
 *          - UploadEngine::Impl ↔ MegaClient + MegaApp (OK):
 *            invalidatePinnedSessionUrl posts a lambda capturing
 *            client.wsPostToClientThread/client.app/client.transfercacheadd;
 *            both reached via wsupload_engine.h's transitive includes.
 *
 *        Refresh-latch out-of-class definitions
 *        (clearRefreshLatchForInstance / applyRefreshResultForInstance) live
 *        in wsupload.cpp, NOT here — they sit next to the public UploadEngine
 *        facade methods and only need the WsPoolMgr declaration.
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

// Full UploadEngine::Impl declaration. Transitively pulls in
// mega/transfer/ws/wsupload_internal.h (WsPool, WsConn, WsPoolThread,
// SteadyTime, ScopedUnlock, WSUPLOAD_TRACE, FailReason),
// mega/transfer/ws/ws_upload_file.h (WsUploadFile complete type),
// mega/megaapp.h (MegaApp complete — needed by invalidatePinnedSessionUrl
// lambda below), and mega/megaclient.h (MegaClient::wsPostToClientThread,
// TransferDbCommitter).
#include "mega/transfer/ws/wsupload_engine.h"

#include "mega/testhooks.h" // DEBUG_TEST_HOOK_WS_SESSION_URL_TRANSITION
#include "mega/utils.h" // Utils::getenv (cross-platform env read for wsLossRecoveryEnvDefault)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib> // std::strtol (wsDatasetConnLimitOverrideEnvDefault)
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace mega
{
namespace ws
{

// Loss-recovery MASTER kill-switch. Default ON. Set MEGA_WS_LOSS_RECOVERY=0 to reproduce
// pre-fix behavior in the same binary (bench A/B): every sub-knob below is ANDed with this
// value in the Impl ctor, so =0 forces them all OFF in one flip. Read exactly once per
// process via a function-local static so repeated engine construction does not re-hit the
// OS env.
bool wsLossRecoveryEnvDefault()
{
    static const bool value = []
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_LOSS_RECOVERY");
        // Default ON when unset. Only an explicit "0" disables; any other value keeps it on.
        return !(hasValue && raw == "0");
    }();
    return value;
}

// Sub-knob readers. Each mirrors wsLossRecoveryEnvDefault() exactly: read its env var once
// via a function-local static, default ON, only an explicit "0" disables. The effective
// per-knob value (computed in the Impl ctor) is `wsXxxEnvDefault() && wsLossRecoveryEnvDefault()`
// so the master kill above forces every sub-knob OFF regardless of its own env var.

// Gates T1b loss-adaptive handshake timeout (WsConn::connectWS, ws_conn.cpp).
bool wsAdaptiveHandshakeEnvDefault()
{
    static const bool value = []
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_ADAPTIVE_HANDSHAKE");
        return !(hasValue && raw == "0");
    }();
    return value;
}

// Gates D opcode-2 (AlreadyOnServer) queued-resend purge (WsConn::onmessage, ws_conn.cpp).
bool wsResendDedupEnvDefault()
{
    static const bool value = []
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_RESEND_DEDUP");
        return !(hasValue && raw == "0");
    }();
    return value;
}

// Gates A whole-chunk-boundary acked-range rewind (WsConn::onmessage markRangeAcked +
// WsPool::retryChunksOnTheWireLocked skip-already-acked). Harmless (never skips an un-acked
// range); kept behind a flag so the upcoming multi-conn conn-count sweep can toggle it.
bool wsAckedRewindEnvDefault()
{
    static const bool value = []
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_ACKED_REWIND");
        return !(hasValue && raw == "0");
    }();
    return value;
}

// Gates the distress/refresh-churn throttle (WsPoolMgr::refreshPools, ws_pool_mgr.cpp).
bool wsRefreshThrottleEnvDefault()
{
    static const bool value = []
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_REFRESH_THROTTLE");
        return !(hasValue && raw == "0");
    }();
    return value;
}

// Gates Design A parallel handshakes (WsConn::connectWS runs the handshake on its own worker
// thread instead of the serial client-thread Baton path). See ws_conn.cpp.
bool wsParallelHandshakeEnvDefault()
{
    static const bool value = []
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_PARALLEL_HANDSHAKE");
        return !(hasValue && raw == "0");
    }();
    return value;
}

// Gates the loss-gated connection-count bump (WsPool::lossBoostedConnLimitLocked,
// ws_pool.cpp; wired into WsPoolMgr::checkPools). Default ON; only an explicit "0"
// disables. ANDed with mLossRecovery in the Impl ctor so MEGA_WS_LOSS_RECOVERY=0 forces
// the legacy no-boost path.
bool wsLossConnBumpEnvDefault()
{
    static const bool value = []
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_LOSS_CONN_BUMP");
        return !(hasValue && raw == "0");
    }();
    return value;
}

// Gates the loss-gated DATASET connection-count bump (A24, SDK-5360 fu8 Session 5;
// WsPool::lossBoostedConnLimitLocked dataset branch, wired into WsPoolMgr::checkPools).
// Default ON; only an explicit "0" disables. ANDed with mLossRecovery in the Impl ctor so
// MEGA_WS_LOSS_RECOVERY=0 forces the legacy no-dataset-boost path. Decoupled from
// MEGA_WS_LOSS_CONN_BUMP at the runtime gate so either bump can be A/B-toggled independently
// in one binary (the lone-small-file bump and the dataset bump are separate levers).
bool wsDatasetConnBumpEnvDefault()
{
    static const bool value = []
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_DATASET_CONN_BUMP");
        return !(hasValue && raw == "0");
    }();
    return value;
}

// Gates the env-gated SINGLE-FILE connection ramp (SDK-5360 fu8 Session 7, Lever C;
// WsPool::lossBoostedConnLimitLocked dataset branch). DEFAULT OFF -- unlike the loss-recovery
// sub-knobs above (default ON, "0" disables) this one defaults OFF and only an explicit "1"
// enables, because it changes single-file behaviour. ANDed with mLossRecovery in the Impl ctor
// (like mDatasetConnBump) so MEGA_WS_LOSS_RECOVERY=0 also forces it off. When ON, a lone-file
// pool (mNumPoolFiles==1) may reach the dataset-boosted ceiling and ride the goodput gate.
bool wsSingleFileConnBumpEnvDefault()
{
    static const bool value = []
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_SINGLEFILE_CONN_BUMP");
        return hasValue && raw == "1";
    }();
    return value;
}

// Runtime NUMERIC override for the dataset boosted connection limit (default 0 = use the
// compile-time kLossBoostedDatasetConnLimit=32). Lets the Queue-B proof bench sweep K (24/32/36)
// on ONE binary without a rebuild. Read once per process. A value above the global ceiling
// lifts the effective ceiling to fit (lossBoostedGlobalConnCeiling) so a high-K bench arm is
// measurable. Clamped to [1, 255]; a non-numeric or <=0 value keeps the default constant.
unsigned char wsDatasetConnLimitOverrideEnvDefault()
{
    static const unsigned char value = []() -> unsigned char
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_DATASET_CONN_LIMIT");
        if (!hasValue)
            return 0;
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || v <= 0)
            return 0;
        return static_cast<unsigned char>(std::min<long>(v, 255));
    }();
    return value;
}

// Gates the goodput-saturation GATE (SDK-5360; WsPoolMgr::checkPools ramp controller). Default
// ON; only an explicit "0" disables. ANDed with mLossRecovery in the Impl ctor, mirroring
// MEGA_WS_DATASET_CONN_BUMP. When ON, the dataset bump's target is approached via a goodput-
// gain-gated RAMP (clean links stay at the low default; only loss-limited datasets widen).
// When OFF (=0), checkPools falls back to TODAY's behaviour: an unconditional jump straight to
// the dataset limit K -- so the gate-vs-unconditional A/B is one binary flip.
bool wsDatasetConnGateEnvDefault()
{
    static const bool value = []
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_DATASET_CONN_GATE");
        return !(hasValue && raw == "0");
    }();
    return value;
}

// Numeric tuning override (MILLISECONDS) for the goodput-gate sampling window. Default 1000ms
// (the controller acts at most once per window, NOT every checkPools tick). Read once per
// process (function-local static). A non-numeric / <=0 value keeps the default; clamped to a
// sane [1ms, 600000ms] range. Consumed via UploadEngine::Impl::gateWindowDs().
unsigned wsGateWindowMsEnvDefault()
{
    static const unsigned value = []() -> unsigned
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_GATE_WINDOW_MS");
        if (!hasValue)
            return 1000u;
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || v <= 0)
            return 1000u;
        return static_cast<unsigned>(std::min<long>(v, 600000));
    }();
    return value;
}

// Numeric tuning override (PERCENT) for the goodput GAIN threshold that justifies keeping /
// adding a connection. Default 5 (%). A +step probe must lift aggregate goodput by at least
// this fraction or it is retreated. Read once per process; a non-numeric / negative value
// keeps the default; clamped to [0, 1000] %.
unsigned wsGateGainPctEnvDefault()
{
    static const unsigned value = []() -> unsigned
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_GATE_GAIN_PCT");
        if (!hasValue)
            return 5u;
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || v < 0)
            return 5u;
        return static_cast<unsigned>(std::min<long>(v, 1000));
    }();
    return value;
}

// Numeric tuning override for the ramp STEP (connections added/removed per probe). Default 1
// (a pure +1/-1 hill-climb). Read once per process; a non-numeric / <=0 value keeps the
// default; clamped to [1, 255].
unsigned char wsGateStepEnvDefault()
{
    static const unsigned char value = []() -> unsigned char
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_GATE_STEP");
        if (!hasValue)
            return 1;
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || v <= 0)
            return 1;
        return static_cast<unsigned char>(std::min<long>(v, 255));
    }();
    return value;
}

// ---- QCT-K gate v2 tunables (SDK-5360 fu8 S8). Each mirrors wsGateGainPctEnvDefault(): read the
// env var once via a function-local static, apply the documented default and clamp. Consumed by
// WsPool::runGoodputGateLocked. Knobs whose valid range includes 0 (TRIM/CEILING/TELEMETRY, where
// 0 = off) accept 0; the others floor to their range minimum.

// MEGA_WS_GATE_BP_QUORUM_PCT (default 50, clamp [1,100]): % of open conns backpressured at a tick
// for that tick to count as quorum-true. Floor 1 (0 refused — it would make MassNotify engage).
unsigned wsGateBpQuorumPctEnvDefault()
{
    static const unsigned value = []() -> unsigned
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_GATE_BP_QUORUM_PCT");
        if (!hasValue)
            return 50u;
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || v <= 0)
            return 50u;
        return static_cast<unsigned>(std::clamp<long>(v, 1, 100));
    }();
    return value;
}

// MEGA_WS_GATE_ENGAGE_WINDOWS (default 2, clamp [1,60]): consecutive quorum-true windows to engage
// from base (anti-stampede debounce).
unsigned wsGateEngageWindowsEnvDefault()
{
    static const unsigned value = []() -> unsigned
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_GATE_ENGAGE_WINDOWS");
        if (!hasValue)
            return 2u;
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || v <= 0)
            return 2u;
        return static_cast<unsigned>(std::clamp<long>(v, 1, 60));
    }();
    return value;
}

// MEGA_WS_GATE_PROBE_WINDOWS (default 2, clamp [1,10]): probe measurement horizon in windows
// (goodput averaged over it; 1 restores S7 single-window judging).
unsigned wsGateProbeWindowsEnvDefault()
{
    static const unsigned value = []() -> unsigned
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_GATE_PROBE_WINDOWS");
        if (!hasValue)
            return 2u;
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || v <= 0)
            return 2u;
        return static_cast<unsigned>(std::clamp<long>(v, 1, 10));
    }();
    return value;
}

// MEGA_WS_GATE_RETREAT_MAX_WINDOWS (default 300, clamp [5,3000]): cap for the exponential
// failed-probe backoff (base 5 windows == S7's retreat cooldown).
unsigned wsGateRetreatMaxWindowsEnvDefault()
{
    static const unsigned value = []() -> unsigned
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_GATE_RETREAT_MAX_WINDOWS");
        if (!hasValue)
            return 300u;
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || v <= 0)
            return 300u;
        return static_cast<unsigned>(std::clamp<long>(v, 5, 3000));
    }();
    return value;
}

// MEGA_WS_GATE_TRIM_WINDOWS (default 10, clamp [0,255]): consecutive quorum-false windows before
// each halving trim; 0 = trim off (S7 never-shrink). Capped at 255 (not the table's 600) because
// the debounce counter WsPool::mGateNoBpWindows is a uint8_t (see wsupload_internal.h).
unsigned wsGateTrimWindowsEnvDefault()
{
    static const unsigned value = []() -> unsigned
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_GATE_TRIM_WINDOWS");
        if (!hasValue)
            return 10u;
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || v < 0) // 0 is valid (= off)
            return 10u;
        return static_cast<unsigned>(std::clamp<long>(v, 0, 255));
    }();
    return value;
}

// MEGA_WS_GATE_MIN_EVENTS (default 8, clamp [1,64], amendment A1): minimum server-confirm events
// per probe judgment (see wsupload_engine.h field doc).
unsigned wsGateMinEventsEnvDefault()
{
    static const unsigned value = []() -> unsigned
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_GATE_MIN_EVENTS");
        if (!hasValue)
            return 8u;
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || v <= 0)
            return 8u;
        return static_cast<unsigned>(std::clamp<long>(v, 1, 64));
    }();
    return value;
}

// MEGA_WS_GATE_CEILING_MULT (default 4, clamp [0,32]): effective ceiling = min(ceilingIn,
// base*mult), in-gate. 0 disables (use ceilingIn as-is).
unsigned char wsGateCeilingMultEnvDefault()
{
    static const unsigned char value = []() -> unsigned char
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_GATE_CEILING_MULT");
        if (!hasValue)
            return 4;
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || v < 0) // 0 is valid (= disable the mult)
            return 4;
        return static_cast<unsigned char>(std::clamp<long>(v, 0, 32));
    }();
    return value;
}

// MEGA_WS_CONN_TELEMETRY_MS (default 10000, clamp [0,600000], 0=off): period of the
// [WsConnTelemetry] per-pool line in WsPoolMgr::checkPools (both gate states).
unsigned wsConnTelemetryMsEnvDefault()
{
    static const unsigned value = []() -> unsigned
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_CONN_TELEMETRY_MS");
        if (!hasValue)
            return 10000u;
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || v < 0) // 0 is valid (= off)
            return 10000u;
        return static_cast<unsigned>(std::clamp<long>(v, 0, 600000));
    }();
    return value;
}

// Runtime numeric override (MILLISECONDS) for the ack-stall watchdog window (SDK-5360 fu8
// Session 6), converted to deciseconds. Default 0 = use the compile-time WsPool::ACKSTALLTIMEOUT
// (45s). Lets the Goal-2d pre/post bench sweep the threshold on ONE binary via
// MEGA_WS_ACKSTALL_TIMEOUT_MS. Read exactly once per process (function-local static).
dstime wsAckStallTimeoutDsEnvDefault()
{
    static const dstime value = []() -> dstime
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_ACKSTALL_TIMEOUT_MS");
        if (!hasValue)
            return 0;
        char* end = nullptr;
        const long ms = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || ms <= 0)
            return 0;
        // dstime is deciseconds (1/10 s); floor at 1ds so a tiny positive value still arms.
        const long ds = ms / 100;
        return static_cast<dstime>(ds > 0 ? ds : 1);
    }();
    return value;
}

// Gates the ack-stall watchdog (WsPoolMgr::checkPools force-reconnect of a silently-hung OPEN
// conn; SDK-5360 fu8 Session 6). Default ON; only an explicit "0" disables. ANDed with
// mLossRecovery in the Impl ctor (so MEGA_WS_LOSS_RECOVERY=0 reproduces full pre-fix behavior)
// AND independently toggleable via MEGA_WS_ACKSTALL_WATCHDOG=0 for the Goal-2d watchdog-off vs
// watchdog-on A/B in one binary. Read exactly once per process (function-local static).
bool wsAckStallWatchdogEnvDefault()
{
    static const bool value = []
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_ACKSTALL_WATCHDOG");
        return !(hasValue && raw == "0");
    }();
    return value;
}

// Independent enable for the tail-completion watchdog (SDK-5360 fu8 S11; the S10 ledger
// flagged that the tail watchdog had no dedicated kill-switch — MEGA_WS_LOSS_RECOVERY=0
// silently disabled a correctness net). Default ON; only an explicit "0" disables. ANDed at
// the checkPools gate with mAckStallWatchdog (which itself carries the mLossRecovery AND), so
// the pre-existing OFF arms (MEGA_WS_ACKSTALL_WATCHDOG=0 / MEGA_WS_LOSS_RECOVERY=0) keep
// disabling BOTH watchdogs byte-identically; this knob only ever narrows. Read exactly once
// per process (function-local static).
bool wsTailCompletionWatchdogEnvDefault()
{
    static const bool value = []
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_TAILCOMPLETION_WATCHDOG");
        return !(hasValue && raw == "0");
    }();
    return value;
}

// Runtime NUMERIC override (MILLISECONDS) for the per-attempt WS handshake timeout (SDK-5360
// fu8 Session 7, Lever A). Default 0 = use the compile-time UploadEngine::Impl constants
// kHandshakeTimeoutLossMs (45s, loss-adaptive) / kHandshakeTimeoutMs (15s, baseline) selected
// in WsConn::connectWS. Lets the bad-network bench sweep the per-attempt timeout on ONE binary.
// Read exactly once per process (function-local static). A non-numeric / <=0 value keeps the
// default constants. Consumed via UploadEngine::Impl::handshakeTimeoutMs(). MUST stay < the
// coupled fail-window (MEGA_WS_HANDSHAKE_FAIL_WINDOW_MS) or one attempt blows the failure budget.
long wsHandshakeTimeoutMsEnvDefault()
{
    static const long value = []() -> long
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_HANDSHAKE_TIMEOUT_MS");
        if (!hasValue)
            return 0;
        char* end = nullptr;
        const long ms = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || ms <= 0)
            return 0;
        return ms;
    }();
    return value;
}

// Runtime NUMERIC override (MILLISECONDS) for the sustained-handshake-failure escalation window
// (SDK-5360 fu8 Session 7, Lever A), converted to deciseconds. Default 0 = use the compile-time
// WsPool::HANDSHAKEFAILTIMEOUT (60s). Coupled to MEGA_WS_HANDSHAKE_TIMEOUT_MS: the per-attempt
// handshake timeout MUST stay UNDER this window or a single attempt blows the whole failure
// budget, so the two are tuned together. Read exactly once per process (function-local static).
// A non-numeric / <=0 value keeps the default. Consumed via
// UploadEngine::Impl::handshakeFailWindowDs().
dstime wsHandshakeFailWindowDsEnvDefault()
{
    static const dstime value = []() -> dstime
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_HANDSHAKE_FAIL_WINDOW_MS");
        if (!hasValue)
            return 0;
        char* end = nullptr;
        const long ms = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || ms <= 0)
            return 0;
        // dstime is deciseconds (1/10 s); floor at 1ds so a tiny positive value still arms.
        const long ds = ms / 100;
        return static_cast<dstime>(ds > 0 ? ds : 1);
    }();
    return value;
}

// Runtime NUMERIC override (MILLISECONDS) for the tail-completion watchdog window (SDK-5360
// fu8 Session 9), converted to deciseconds. Default 0 = use the compile-time
// WsPool::TAILCOMPLETIONTIMEOUT (60s). Read exactly once per process (function-local static).
// A non-numeric / <=0 value keeps the default. Consumed via
// UploadEngine::Impl::tailCompletionTimeoutDs().
dstime wsTailCompletionTimeoutDsEnvDefault()
{
    static const dstime value = []() -> dstime
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_TAIL_COMPLETION_TIMEOUT_MS");
        if (!hasValue)
            return 0;
        char* end = nullptr;
        const long ms = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str() || ms <= 0)
            return 0;
        // dstime is deciseconds (1/10 s); floor at 1ds so a tiny positive value still arms.
        const long ds = ms / 100;
        return static_cast<dstime>(ds > 0 ? ds : 1);
    }();
    return value;
}

// Gates candidate 3c (small-file cold-start cap=1) read in
// WsPool::coldStartHandshakeCapLocked (ws_pool.cpp). Default ON; only an explicit "0"
// disables. INDEPENDENT of MEGA_WS_LOSS_RECOVERY (see wsupload_engine.h): a clean-network
// single-small-file overhead optimisation, not loss-recovery, so it is NOT ANDed with the
// master kill in the Impl ctor. Read exactly once per process via a function-local static.
bool wsSmallFileColdStartEnvDefault()
{
    static const bool value = []
    {
        const auto [raw, hasValue] = Utils::getenv("MEGA_WS_SMALLFILE_COLDSTART");
        return !(hasValue && raw == "0");
    }();
    return value;
}

// ========== Lifecycle ==========

UploadEngine::Impl::~Impl()
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

void UploadEngine::Impl::stop()
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

void UploadEngine::Impl::start()
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
        if (!pool)
            continue;

        // Non-retiring pools always re-arm to the configured pool limit.
        // Retiring pools re-arm only if they still own work — otherwise they
        // would be picked up by cleanupRetiringPools on the next manager-thread
        // tick. The active-upload case fixes a stop()/start() race where a
        // server-side Distress (opcode 5) had retired the original size-class
        // pool serving an in-flight file, and applyRefreshedUrls had not yet
        // migrated the file off that pool because
        // UploadEngine::Impl::nextEligible() reserves a bound file to f->mPool.
        const bool retiringHasWork = pool->mRetiring &&
            (pool->mUploadingFile || pool->mNumPoolFiles ||
             pool->mNumChunksInFlight || !pool->mToResend.empty());
        if (!pool->mRetiring || retiringHasWork)
            pool->setPoolNumConn(mPoolConnectionLimit);
    }
    poolMgr.refreshPools();

    uploadThread = std::thread(
        [this]
        {
            this->run();
        });
}

void UploadEngine::Impl::kick()
{
    WSUPLOAD_TRACE << "[UploadEngine::Impl::kick] BEGIN [this = " << this << "]";
    std::lock_guard<std::mutex> g(uploadMutex);
    for (auto& p: poolMgr.mPools)
    {
        WSUPLOAD_TRACE << "[UploadEngine::Impl::kick] pool(" << (void*)p.get()
                  << ") checkThreads() [this = " << this << "]";
        p->checkThreads();
    }
    WSUPLOAD_TRACE << "[UploadEngine::Impl::kick] END [numPools=" << poolMgr.mPools.size()
              << "] [this = " << this << "]";
}

// Manager thread
void UploadEngine::Impl::run()
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

// ========== Queue mutation ==========

void UploadEngine::Impl::enqueue(Transfer& t)
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

void UploadEngine::Impl::reposition(Transfer& t, Transfer* before)
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

void UploadEngine::Impl::remove(Transfer& t)
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

// Linear-scan fileList for f and erase it, rebasing nextIt to the successor on hit.
// Caller must hold uploadMutex.
void UploadEngine::Impl::eraseFromFileListLocked(WsUploadFile* f)
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

// ========== Per-file commands ==========

void UploadEngine::Impl::pause(Transfer& t)
{
    withFile(t,
             [](WsUploadFile& f)
             {
                 f.setPaused(true);
             });
}

void UploadEngine::Impl::unpause(Transfer& t)
{
    withFile(t,
             [](WsUploadFile& f)
             {
                 f.setPaused(false);
             });
}

void UploadEngine::Impl::setRetryUntil(Transfer& t, const dstime when)
{
    withFile(t,
             [when](WsUploadFile& f)
             {
                 f.setRetryUntil(when);
             });
}

void UploadEngine::Impl::markFailed(Transfer& t, const dstime retryUntil)
{
    withFile(t,
             [retryUntil](WsUploadFile& f)
             {
                 f.markFailedForRetry(retryUntil);
             });
}

// ========== Per-file queries ==========

bool UploadEngine::Impl::isUploading(Transfer& t) const
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

bool UploadEngine::Impl::isUploading(const Transfer& t) const
{
    std::lock_guard<std::mutex> g(uploadMutex);
    auto it = files.find(const_cast<Transfer*>(&t));
    if (it == files.end() || !it->second)
        return false;
    const WsUploadFile* f = it->second.get();
    return f->inPool();
}

bool UploadEngine::Impl::getTransferStats(const Transfer& t,
                                          UploadEngine::WsTransferStats& stats) const
{
    std::lock_guard<std::mutex> g(uploadMutex);
    auto it = files.find(const_cast<Transfer*>(&t));
    if (it == files.end() || !it->second)
        return false;
    return it->second->getTransferStats(stats);
}

bool UploadEngine::Impl::drainConfirmedChunkMacs(Transfer& t, std::vector<chunkmac_map>& out)
{
    std::lock_guard<std::mutex> g(uploadMutex);
    const auto it = files.find(&t);
    if (it == files.end() || !it->second)
        return false;
    it->second->drainConfirmedChunkMacs(out);
    return true;
}

bool UploadEngine::Impl::getSessionUrl(Transfer& t, std::string& outUrl) const
{
    std::lock_guard<std::mutex> g(uploadMutex);
    const auto it = files.find(&t);
    if (it == files.end() || !it->second)
    {
        return false;
    }
    return it->second->getCurrentSessionUrl(outUrl);
}

// ========== Test-only queries (always-compile signatures, gated bodies) ==========

bool UploadEngine::Impl::getPoolStateForTesting(const std::string& url,
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
        // C-7 gate mirrors (last-writer-wins; exact at 1 worker/pool — see wsupload.h).
        out.gateRetryCount = pool.mGateRetryCountForTesting;
        out.gateNullCandidateStreak = pool.mGateNullCandidateStreakForTesting;
        out.gateEvaluationCount = pool.mGateEvaluationCountForTesting;
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

bool UploadEngine::Impl::getWsUploadStatsForTesting(
    UploadEngine::WsUploadStatsForTesting& out) const
{
    out = {};
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    std::lock_guard<std::mutex> g(uploadMutex);
#ifndef NDEBUG
    // N4 fix: seed the aggregate from retired pools' folded stats so the numbers SURVIVE a
    // refreshPools()-driven pool replacement (a refresh shortly before this snapshot used to
    // zero the whole block). Live pools are folded ON TOP below via the same += / max logic,
    // so cumulative counters add correctly and high-waters take the overall max. poolCount is
    // reset to count LIVE pools only (the summed counters remain cumulative incl. retired).
    out = poolMgr.mRetiredPoolStats;
#endif
    out.poolCount = 0;
    out.found = true;
    out.refreshPoolsCount = poolMgr.mRefreshPoolsCount;
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

UploadEngine::BenchThrottleSnapshot UploadEngine::Impl::getAndResetBenchThrottleStats()
{
    UploadEngine::BenchThrottleSnapshot out;
    // Snapshot+reset is Release-safe and always-compiled: it reads only the
    // `WsPool::mBenchThrottle*` atomics (always present, no NDEBUG/hook gating).
    // `uploadMutex` is held to stabilize `poolMgr.mPools` against
    // creation/teardown for the duration of the iteration; per-counter resets
    // remain `memory_order_relaxed`.
    std::lock_guard<std::mutex> g(uploadMutex);
    for (const auto& poolPtr: poolMgr.mPools)
    {
        if (poolPtr)
        {
            poolPtr->addAndResetBenchThrottleStatsTo(out);
        }
    }
    return out;
}

bool UploadEngine::Impl::isTrackedForTesting(const Transfer& t) const
{
    std::lock_guard<std::mutex> g(uploadMutex);
    return files.find(const_cast<Transfer*>(&t)) != files.end();
}

std::uintptr_t UploadEngine::Impl::getFilePoolIdForTesting(Transfer& t) const
{
    std::lock_guard<std::mutex> g(uploadMutex);
    if (const auto it = files.find(&t);
        it != files.end() && it->second && it->second->hasPool())
    {
        return reinterpret_cast<std::uintptr_t>(it->second->mPool);
    }
    return 0;
}

// ========== URL invalidation ==========

void UploadEngine::Impl::invalidatePinnedSessionUrl(const std::string& url)
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

// ========== Notifications / connection limits ==========

void UploadEngine::Impl::notifyWorkers()
{
    std::lock_guard<std::mutex> g(uploadMutex);
    notifyWorkersLocked();
}

void UploadEngine::Impl::notifyNetworkDisconnect()
{
    disconnectEpoch.fetch_add(1, std::memory_order_release);
    notifyWorkers();
}

void UploadEngine::Impl::setMaxConnections(const unsigned char maxConnections)
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

bool UploadEngine::Impl::consumeUploadBudget(const m_off_t bytes,
                                             dstime* retryAfterDs,
                                             const void* requesterKey)
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
        // S12 Cluster-B fix v3: the cap must accommodate the LARGEST currently-waiting
        // need, not just the transient caller's. maxBurstBudget (speed×5 s) is smaller
        // than a chunk at low caps, so a frequent smaller-need poller otherwise clamps
        // the shared budget below a larger-need pool's threshold FOREVER (the
        // deterministic starvation of the stress repro — no waiter scheme can accrue
        // past a clamp applied by its competitor's polls).
        m_off_t largestPendingNeed = bytes;
        if (!mBudgetWaiters.empty())
        {
            largestPendingNeed = std::max(largestPendingNeed, mBudgetWaiters.front().bytes);
        }
        const m_off_t budgetCap = std::max(maxBurstBudget, largestPendingNeed);
        if (mUploadBudget > budgetCap)
        {
            mUploadBudget = budgetCap;
        }
        mUploadBudgetLastDs = now;
    }

    // S12 Cluster-B fix v3 — strict-FIFO fairness. All-or-nothing global grants with
    // phase-locked pollers starve an equal-need sibling forever (deterministic repro);
    // a v2-style single reservation merely inverted the starvation. Under contention,
    // grants go STRICTLY in registration order: non-front askers register (once) and
    // fail even when the budget momentarily suffices — the front waiter is always served
    // first, so every asker's wait is bounded by (queue position × per-grant accrual).
    constexpr dstime kBudgetWaiterStaleDs = 100;
    constexpr std::size_t kBudgetWaiterCap = 16;
    // Expire waiters that stopped asking (retired pool, paused/completed file).
    for (auto it = mBudgetWaiters.begin(); it != mBudgetWaiters.end();)
    {
        if (SteadyTime::difference(now, it->lastAskDs) > kBudgetWaiterStaleDs)
        {
            it = mBudgetWaiters.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (requesterKey)
    {
        auto me = std::find_if(mBudgetWaiters.begin(),
                               mBudgetWaiters.end(),
                               [requesterKey](const BudgetWaiter& w)
                               {
                                   return w.key == requesterKey;
                               });
        if (me != mBudgetWaiters.end())
        {
            me->lastAskDs = now;
            me->bytes = bytes;
        }
        const bool queueEmpty = mBudgetWaiters.empty();
        const bool amFront = !queueEmpty && mBudgetWaiters.front().key == requesterKey;
        if (queueEmpty || amFront)
        {
            if (mUploadBudget >= bytes)
            {
                if (amFront)
                {
                    mBudgetWaiters.erase(mBudgetWaiters.begin()); // served
                }
                mUploadBudget -= bytes;
                return true;
            }
            if (queueEmpty)
            {
                mBudgetWaiters.push_back({requesterKey, bytes, now});
            }
        }
        else if (me == mBudgetWaiters.end() && mBudgetWaiters.size() < kBudgetWaiterCap)
        {
            mBudgetWaiters.push_back({requesterKey, bytes, now});
        }
        if (retryAfterDs)
        {
            const m_off_t deficit =
                bytes > mUploadBudget ? bytes - mUploadBudget : static_cast<m_off_t>(1);
            const m_off_t numerator =
                deficit * static_cast<m_off_t>(SpeedController::DS_PER_SECOND) + mMaxUploadSpeed - 1;
            const dstime suggestedDs = static_cast<dstime>(numerator / mMaxUploadSpeed);
            *retryAfterDs = std::clamp<dstime>(suggestedDs, 1, 10);
        }
        return false;
    }

    // Legacy keyless path (no current callers): plain all-or-nothing.
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

void UploadEngine::Impl::refundUploadBudget(const m_off_t bytes)
{
    if (bytes <= 0 || mMaxUploadSpeed <= 0)
    {
        return;
    }

    const m_off_t maxValue = std::numeric_limits<m_off_t>::max();
    const m_off_t burstWindowSeconds =
        static_cast<m_off_t>(SpeedController::SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS);
    const m_off_t maxBurstBudget = (mMaxUploadSpeed > (maxValue / burstWindowSeconds)) ?
                                       maxValue :
                                       (mMaxUploadSpeed * burstWindowSeconds);
    // Same v3 clamp rule as consumeUploadBudget: never clamp below the front waiter's
    // pending need (a refund must not destroy budget a waiter is accruing toward).
    m_off_t largestPendingNeed = bytes;
    if (!mBudgetWaiters.empty())
    {
        largestPendingNeed = std::max(largestPendingNeed, mBudgetWaiters.front().bytes);
    }
    const m_off_t budgetCap = std::max(maxBurstBudget, largestPendingNeed);
    if (bytes > (maxValue - mUploadBudget))
    {
        mUploadBudget = budgetCap;
        return;
    }
    mUploadBudget = std::min(mUploadBudget + bytes, budgetCap);
}

void UploadEngine::Impl::setMaxUploadSpeed(const m_off_t bytesPerSecond)
{
    std::lock_guard<std::mutex> g(uploadMutex);
    mMaxUploadSpeed = std::max<m_off_t>(bytesPerSecond, 0);
    mUploadBudget = 0;
    mUploadBudgetLastDs = SteadyTime::ds();
}

// ========== Scheduling ==========

WsUploadFile* UploadEngine::Impl::nextEligible(const m_off_t min,
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

bool UploadEngine::Impl::hasEligibleFileForPool(const m_off_t min,
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

unsigned UploadEngine::Impl::eligibleFileCountForPoolCappedLocked(
    const m_off_t min,
    const m_off_t max,
    const std::string* requiredSessionUrl,
    const WsPool* requestingPool,
    const unsigned cap,
    m_off_t* firstEligibleSize) const
{
    // Mirrors hasEligibleFileForPool's eligibility predicate exactly, but counts hits
    // (early-exit at the cap-th) instead of returning on the first. Candidate 3c uses
    // the count to distinguish "one queued small file" (cap=1 cold start) from a burst
    // (>=2 eligible -> keep COLDSTART_HANDSHAKE_CONNS). The first eligible file's ACTUAL
    // size() is captured so the caller can apply the small-file ceiling (the size-class
    // ceiling mMaxFileSize is NOT the file size). cap==0 returns 0 immediately.
    unsigned count = 0;
    for (WsUploadFile* f: fileList)
    {
        if (count >= cap)
        {
            break;
        }
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
            if (count == 0 && firstEligibleSize)
            {
                *firstEligibleSize = f->size();
            }
            ++count;
        }
    }

    return count;
}

// ========== Worker bookkeeping ==========

void UploadEngine::Impl::cleanupExitedPoolThreads(std::unique_lock<std::mutex>& lk)
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

void UploadEngine::Impl::bumpQueueVersion()
{
    const std::uint32_t newVersion =
        queueVersion.fetch_add(1, std::memory_order_relaxed) + 1;
    for (auto& pool: poolMgr.mPools)
    {
        if (pool && pool->mUploadingFile && inQueue.count(pool->mUploadingFile))
            pool->mUFTQversion = newVersion;
    }
}

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD
