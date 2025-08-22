#pragma once

#include <memory>

namespace mega
{

class MegaClient;
struct Transfer;
class File;

namespace ws
{

/**
 * UploadEngine: WebSocket upload executor for PUT transfers.
 * Phase 1: mirrors TransferList ordering/priority, reads via FileAccess,
 *          executes using an internal queue/pool/thread model (prototype).
 * Phase 3+: WebSocket handles will migrate into CurlHttpIO (single IO reactor).
 */
class UploadEngine
{
public:
    explicit UploadEngine(MegaClient& client);
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
    bool isUploading(Transfer& t) const;

    // Hint the engine that conditions may have changed (e.g. FA queue).
    void kick();

    struct Impl; // pImpl keeps heavy includes out of headers

private:
    std::unique_ptr<Impl> pImpl;
};

// Small helper to flip PUTs to ws mode
bool wsEnabled(const MegaClient&);

} // namespace ws
} // namespace mega
