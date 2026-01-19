#pragma once

#include "types.h"

#include <chrono>
#include <functional>
#include <memory>

namespace mega
{

class MegaClient;
struct Transfer;

// class File;

namespace ws
{

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

    // integration
    void setCallbacks(Callbacks cb);
    bool getTransferStats(const Transfer& t, WsTransferStats& stats) const;

    // Hint the engine that conditions may have changed (e.g. FA queue).
    void kick();

    class Impl; // pImpl keeps heavy includes out of headers

private:
    std::unique_ptr<Impl> pImpl;
};

// Small helper to flip PUTs to ws mode
bool wsEnabled(const MegaClient&);

} // namespace ws
} // namespace mega
