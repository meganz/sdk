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
 * Phase 1: queue mirror + size-class pools + WS send.
 * Phase 2: report progress to Transfer/app (callbacks).
 */
class UploadEngine
{
public:
    struct WsTransferStats
    {
        m_off_t meanSpeedBytesPerSecond = 0;
        // Per-transfer circular mean upload speed (SpeedController window, ACK-based).
        m_off_t windowSpeedBytesPerSecond = 0;
        std::chrono::milliseconds avgStartTransferTime{0};
        double failedRequestRatio = 0.0;
    };
    // Callbacks are invoked by the engine thread. In Phase 3 they will run
    // on CurlHttpIO/Waiter. MegaClient should bounce them to its own thread
    // if required.
    struct Callbacks
    {
        // Preflight before starting a new file. Return false to defer the start.
        // Use this to run the upload "prep" that dispatchTransfers() performs for legacy PUTs
        // (e.g., FA scheduling, metadata checks). The engine will keep the file queued and
        // retry later until true is returned.
        std::function<bool(Transfer&)> preflightStart;

        // File selected to start sending (first chunk about to be read)
        std::function<void(Transfer&)> onStart;

        // Cumulative bytes confirmed by the server (ACKed). Called frequently.
        std::function<void(Transfer&, m_off_t confirmed)> onProgress;

        // Terminal failure for this attempt.
        // apierr is typically a negative Mega API error code.
        // aux carries extra WS context (if any).
        std::function<void(Transfer&, int apierr, m_off_t aux)> onFail;

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

    // Bring engine online (spawns manager thread).
    void start();

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

    // Hint WS worker threads to drop current socket sessions and reconnect later.
    // Useful to keep disconnect semantics aligned with legacy HTTP transfers.
    void notifyNetworkDisconnect();

    // Set desired WS per-transfer upload concurrency (worker threads per non-pinned pool).
    void setMaxConnections(unsigned char maxConnections);

#ifndef NDEBUG
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
    };

    bool getPoolStateForTesting(const std::string& url, PoolStateForTesting& out) const;
#endif
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
