#pragma once

#include "types.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mega
{

class MegaClient;
struct Transfer;

// class File;

namespace ws
{

namespace detail
{

// Internal parser helper exposed so unit tests can cover inbound WS frame validation directly.
enum class InboundFrameValidationResult
{
    Ok,
    TooShort,
    BadCrc,
};

// Header used by inbound chunk frames: [fileno:4B][chunkpos:m_off_t][event:1B]
constexpr int kInboundChunkResponseBytes =
    static_cast<int>(sizeof(std::uint32_t) + sizeof(m_off_t) + sizeof(signed char));
constexpr int kInboundFrameTrailerCrcBytes = static_cast<int>(sizeof(std::uint32_t));
constexpr int kMinInboundFrameBytes = kInboundChunkResponseBytes + kInboundFrameTrailerCrcBytes;

InboundFrameValidationResult validateInboundFrame(const char* msg, int len);

} // namespace detail

/**
 * UploadEngine: WebSocket upload executor for PUT transfers.
 * Maintains a queue mirror, size-class pools, WS sends, and progress callbacks.
 */
class UploadEngine
{
public:
    enum class PreflightStartResult : std::uint8_t
    {
        Ready,        // preflight completed and result consumable now
        Pending,      // queued/running (or racing with completion)
        NotScheduled, // rejected before queueing (e.g. back-pressure cap)
    };

    enum class FailureDisposition : std::uint8_t
    {
        Retryable,
        Permanent
    };

    struct WsTransferStats
    {
        m_off_t meanSpeedBytesPerSecond = 0;
        // Per-transfer circular mean upload speed (SpeedController window, ACK-based).
        m_off_t windowSpeedBytesPerSecond = 0;
        std::chrono::milliseconds avgStartTransferTime{0};
        double failedRequestRatio = 0.0;
    };

    // Callbacks are invoked by the engine thread. MegaClient should bounce
    // them to its own thread if required.
    struct Callbacks
    {

        // Preflight before starting a new file.
        // Use this to run the upload "prep" that dispatchTransfers() performs for legacy PUTs
        // (e.g., FA scheduling, metadata checks).
        std::function<PreflightStartResult(Transfer&)> preflightStart;

        // File selected to start sending (first chunk about to be read)
        std::function<void(Transfer&)> onStart;

        // Cumulative bytes confirmed by the server (ACKed). Called frequently.
        std::function<void(Transfer&, m_off_t confirmed)> onProgress;

        // Terminal failure for this attempt.
        // apierr is typically a negative Mega API error code.
        // aux carries extra WS context (if any).
        // disposition indicates whether legacy parity requires suppressing retries.
        std::function<void(Transfer&, int apierr, m_off_t aux, FailureDisposition disposition)>
            onFail;

        // Upload completed; small payload (server metadata) is provided.
        std::function<void(Transfer&, const char* payload, int len)> onComplete;

        // Back‑pressure gate: if provided and returns false, the engine will
        // not start a new file yet (but will keep pumping existing ones).
        std::function<bool()> canStartAnotherFile;
    };

    explicit UploadEngine(MegaClient&);
    ~UploadEngine();

    UploadEngine(const UploadEngine&) = delete;
    UploadEngine& operator=(const UploadEngine&) = delete;

    std::uint64_t instanceId() const noexcept;

    // Internal: clear the "refresh in flight" latch on this engine iff `id` still
    // matches `instanceId()`. Used by the refresh-pools callback orchestration in
    // wsupload.cpp so `WsPoolMgr` does not need friend access to `pImpl`.
    // Acquires `uploadMutex` internally; safe to call from the client thread.
    // Returns true if the latch was cleared (id matched), false otherwise.
    bool clearRefreshLatchForInstance(std::uint64_t id);

    // Internal: apply the outcome of a USC refresh attempt on this engine iff `id`
    // still matches `instanceId()`. On success, resets refresh-fail counters and
    // applies the refreshed URL set; on error, bumps the fail counter and programs
    // exponential backoff. In both cases, clears the "refresh in flight" latch at
    // the end. Acquires `uploadMutex` internally.
    // Returns true if the outcome was applied (id matched), false otherwise.
    bool applyRefreshResultForInstance(std::uint64_t id,
                                       Error e,
                                       std::vector<std::pair<std::string, m_off_t>>&& urls);

    // Bring engine online (spawns manager thread).
    void start();
    // Shutdown engine: stop scheduling new WS work and request worker threads to exit.
    void stop();
    bool isStopping() const;

    // Mirror TransferList semantics (PUT only):
    void enqueue(Transfer& t); // addtransfer()
    void reposition(Transfer& t, Transfer* before /* nullptr=end */);
    void pause(Transfer& t);
    void unpause(Transfer& t);
    void remove(Transfer& t);
    void setRetryUntil(Transfer& t, dstime when);
    void markFailed(Transfer& t, dstime retryUntil);

    // status
    bool isUploading(Transfer& t) const;

    // Drain any server-confirmed chunk MAC updates accumulated on the WS worker threads.
    //
    // The legacy HTTP PUT path updates Transfer::chunkmacs and persists them via
    // MegaClient::transfercacheadd() as chunks are confirmed. WS uploads must do
    // the same work, but without mutating Transfer from worker threads.
    //
    // Returns false if the transfer is not currently tracked by the engine.
    bool drainConfirmedChunkMacs(Transfer& t, std::vector<chunkmac_map>& out);

    // Get the WSS session URL currently used by this transfer (if any).
    //
    // Returns false if the transfer is not tracked or has not been assigned to a pool yet.
    bool getSessionUrl(Transfer& t, std::string& outUrl) const;

    // integration
    void setCallbacks(Callbacks cb);
    bool getTransferStats(const Transfer& t, WsTransferStats& stats) const;

    // Hint the engine that conditions may have changed (e.g. FA queue).
    void kick();

    // Wake worker threads waiting for preflight completion or new work.
    void notifyWorkers();

    // Hint WS worker threads to drop current socket sessions and reconnect later.
    // Useful to keep disconnect semantics aligned with legacy HTTP transfers.
    void notifyNetworkDisconnect();

    // Set desired WS per-transfer upload concurrency (worker threads per non-pinned pool).
    void setMaxConnections(unsigned char maxConnections);

    // Test-only types + accessors below are always-compile (not NDEBUG-gated)
    // so tests/integration/wsupload/headers/WsUploadDebugHelpers.h can include
    // this header in hooks-OFF builds where WSUPLOAD_REQUIRE_TEST_HOOKS()
    // expands to GTEST_SKIP at runtime but the body must still compile.
    // Release-build code-segment cost: ~80 LOC of dead types and forwarder
    // bodies, never reached at runtime.
    struct PoolStateForTesting
    {
        bool found = false;
        bool pinned = false;
        bool retiring = false;
        int numPoolFiles = 0;
        bool hasUploadingFile = false;
        bool hasReference = false;
        int numChunksInFlight = 0;
        unsigned queuedResends = 0;
        unsigned activeThreads = 0;
        unsigned exitingThreads = 0;
        unsigned openConnections = 0;
        unsigned connectionsWithInFlight = 0;
        unsigned maxConnectionsWithInFlightSeen = 0;
        // Exposed for B8 regression test: mPausedByServerUntil (deciseconds). Zero when
        // the pool is not currently throttled by the server.
        dstime pausedByServerUntilDs = 0;
        // C-7 escalation-gate observability (SDK-5360 fu8 S12). The gate's retryCount /
        // firstConnectFailureDs are pool-worker STACK locals; these mirrors are
        // last-writer-wins across workers, exact only at setMaxConnections(1) — the C-7
        // cell pins 1 connection for that reason.
        int gateRetryCount = 0;
        std::uint64_t gateNullCandidateStreak = 0;
        std::uint64_t gateEvaluationCount = 0;
    };

    struct WsUploadStatsForTesting
    {
        bool found = false;
        unsigned poolCount = 0;
        // Cumulative WsPoolMgr::refreshPools() invocations this run (engine-lifetime counter,
        // survives pool retirement). W1/N4 (SDK-5360 fu8): RSS peak tracks refresh-churn
        // VOLUME, not conn count (0/54/94/119 refreshes -> 105/170/169/208 MiB). Surfaced so
        // the bench can attribute peak RSS to churn per run and gate HR54 on churn-free reps.
        std::uint64_t refreshPoolsCount = 0;
        std::uint64_t uploadingFileOccupiedMs = 0;
        std::uint64_t lastAckToNextFirstByteSamples = 0;
        std::uint64_t lastAckToNextFirstByteTotalMs = 0;
        std::uint64_t lastAckToNextFirstByteMaxMs = 0;
        std::uint64_t allChunksInFlightBlockedMs = 0;
        std::uint64_t eligibleFileSampleCount = 0;
        std::uint64_t blockedByInFlightSampleCount = 0;
        std::uint64_t idleEligibleConnectionMs = 0;
        std::uint64_t idleEligibleConnectionSampleCount = 0;

        // Send-side counters (aggregated across pools/conns).
        std::uint64_t curlAgainSendCount = 0;
        std::uint64_t curlAgainRecvCount = 0;
        // E-3: Σ bytes curl_ws_send accepted across all conns. A deterministic
        // mid-chunk force-close test compares this to the file size to measure
        // whole-chunk re-send waste (accepted > fileSize today; ~= fileSize once
        // byte-resume lands).
        std::uint64_t totalCurlWsSendAcceptedBytes = 0;
        // # closeWS() teardowns that ran with a live partial frame (fix #6).
        std::uint64_t partialFrameTornDownCount = 0;
        // Peak concurrent in-flight connections (max across pools) -- the
        // ACTUALLY-USED flow count. Surfaced for the flow-count experiment + bench
        // hygiene: the configured MEGA_BENCH_UPLOAD_CONNECTIONS may exceed the used
        // count (develop caps connections for small files), so the analysis verifies
        // configured == used per row.
        unsigned maxConnectionsWithInFlightSeen = 0;
        std::uint64_t haveSpaceFalseIters = 0;
        std::uint64_t haveSpaceFalseWaitMs = 0;
        std::uint64_t readyForDataFalseIters = 0;
        std::uint64_t readyForDataFalseWaitMs = 0;
        std::uint64_t throttleSleepIters = 0;
        std::uint64_t throttleSleepMs = 0;
        std::uint64_t backlogEmptyIters = 0;
        std::uint64_t backlogEmptyMs = 0;
        std::uint64_t bufferedAmountHighWater = 0;
        std::uint64_t chunksInFlightHighWater = 0;
        std::uint64_t chunkPrepTotalMs = 0;
        std::uint64_t chunkPrepMaxMs = 0;
        std::uint64_t chunkPrepN = 0;

        // Server throttle telemetry.
        std::uint64_t throttleEventCount = 0;
        std::uint64_t throttleEventTotalDs = 0;
        std::uint64_t throttleEventSumSqDs = 0;
        std::uint64_t throttleEventMinDs = 0;
        std::uint64_t throttleEventMaxDs = 0;
        std::uint64_t throttleBucket0to1s = 0;
        std::uint64_t throttleBucket1to5s = 0;
        std::uint64_t throttleBucket5to30s = 0;
        std::uint64_t throttleBucket30sPlus = 0;
        std::uint64_t throttleEventCodeCounts[16] = {};
        std::uint64_t simultaneousThrottledConnsMax = 0;
        std::uint64_t simultaneousThrottledConnsSamples = 0;
        std::uint64_t simultaneousThrottledConnsSum = 0;
        std::uint64_t throttleRecoveryAckSamples = 0;
        std::uint64_t throttleRecoveryAckTotalMs = 0;
        std::uint64_t throttleRecoveryAckMaxMs = 0;
    };

    bool getPoolStateForTesting(const std::string& url, PoolStateForTesting& out) const;
    bool getWsUploadStatsForTesting(WsUploadStatsForTesting& out) const;
    bool isTrackedForTesting(const Transfer& t) const;
    std::uintptr_t getFilePoolIdForTesting(Transfer& t) const;

    // Release-safe per-iter throttle snapshot consumed by the bench framework
    // (`tests/integration/bench_framework/`). Atomically swaps the underlying
    // `WsPool::mBenchThrottle*` accumulators to zero across all pools and
    // returns the previous values, so each iter's snapshot is independent.
    // Always-compile (NOT gated on MEGASDK_DEBUG_TEST_HOOKS_ENABLED) because
    // `MEGA_BENCH_FRAMEWORK_ENABLED` is independent of test-hooks.
    struct BenchThrottleSnapshot
    {
        std::int64_t event6Count = 0;
        std::int64_t event6TotalMs = 0;
        std::int64_t pauseCount = 0;
        std::int64_t pauseTotalMs = 0;
    };
    BenchThrottleSnapshot getAndResetBenchThrottleStats();

    // Set desired WS upload speed limit in bytes per second (<=0 means unlimited).
    void setMaxUploadSpeed(m_off_t bytesPerSecond);

    class Impl; // pImpl keeps heavy includes out of headers

private:
    std::unique_ptr<Impl> pImpl;
};

// Small helper to flip PUTs to ws mode
bool wsEnabled(const MegaClient&);

} // namespace ws
} // namespace mega
