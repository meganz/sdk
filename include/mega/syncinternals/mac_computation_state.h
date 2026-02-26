/**
 * @file mac_computation_state.h
 * @brief State structure for asynchronous local file MAC computation.
 *
 * This struct handles the expensive part of MAC verification: computing
 * the local file's MetaMAC. The comparison with remote MAC is done separately
 * after local MAC is computed.
 *
 * Used for:
 * - CSF (Cloud+Sync+FS) case: mtime-only differences in synced files
 * - Clone candidates: verifying file content before cloning a node
 *
 * @copyright Simplified (2-clause) BSD License.
 */

#ifndef MEGA_MAC_COMPUTATION_STATE_H
#define MEGA_MAC_COMPUTATION_STATE_H

#ifdef ENABLE_SYNC

#include "mega/filefingerprint.h"
#include "mega/localpath.h"
#include "mega/types.h"
#include "mega/utils.h"

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>

namespace mega
{

class MacComputationThrottle;

/**
 * @brief Context for CSF case validation and comparison.
 *
 * Used to detect if the file or cloud node changed during MAC computation.
 * Also stores the expected (remote) MAC for comparison when local MAC is ready.
 * Only needed for CSF case - clone candidates use upload object lifetime instead.
 */
struct MacComputationContext
{
    FileFingerprint localFp;
    FileFingerprint cloudFp;
    NodeHandle cloudHandle;
    handle fsid{UNDEF};
    int64_t expectedMac{INVALID_META_MAC}; // Remote MAC for comparison

    bool matches(const handle currentFsid,
                 const NodeHandle currentCloudHandle,
                 const FileFingerprint& currentLocalFp,
                 const FileFingerprint& currentCloudFp) const
    {
        return currentFsid == fsid && currentCloudHandle == cloudHandle &&
               currentLocalFp.equalExceptMtime(localFp) && currentCloudFp.equalExceptMtime(cloudFp);
    }
};

/**
 * @brief State for asynchronous local file MAC computation.
 *
 * Tracks progress of incremental MAC computation across sync iterations.
 * Thread-safe: sync thread reads/writes, worker thread (mAsyncQueue) computes.
 *
 * Lifetime management:
 * - Owner (LocalNode::RareFields or SyncUpload_inClient) holds shared_ptr
 * - Worker thread captures weak_ptr in lambda
 * - If owner is destroyed, weak_ptr.lock() returns nullptr and computation is abandoned
 *
 * This struct focuses ONLY on computing the local file's MAC.
 * Comparison with remote MAC is done after local MAC is ready.
 */
struct MacComputationState
{
    // File info (immutable after construction)
    const m_off_t totalSize;
    const LocalPath filePath;

    // Cipher params (from reference node - needed to compute MAC)
    std::array<byte, SymmCipher::KEYLENGTH> transferkey{};
    int64_t ctriv = 0;

    // Progress tracking
    m_off_t currentPosition = 0;

    // Accumulated chunk MACs - protected by mutex
    mutable std::mutex macsMutex;
    chunkmac_map partialMacs;

    // Buffer size for reading chunks (10MB)
    static constexpr m_off_t BUFFER_SIZE = 10 * 1024 * 1024;

    // State flags (atomic for thread safety)
    std::atomic<bool> chunkInProgress{false}; // True while worker processing
    std::atomic<bool> completed{false}; // True when local MAC computed
    std::atomic<bool> failed{false}; // True if read/compute error

    // True when initialization is complete (first advanceMacComputation has returned).
    // Used to prevent checkPendingCloneMac from racing with initCloneCandidateMacComputation.
    // The sync thread (checkPendingCloneMac) should not proceed until this is true.
    std::atomic<bool> initializationComplete{false};

    // Throttle tracking - true if we've acquired a slot from MacComputationThrottle
    bool throttleSlotAcquired{false};

    // Result - the computed local file MAC (valid after completed=true)
    int64_t localMac{INVALID_META_MAC};

    // Optional context for CSF case validation
    // Not used for clone candidates (they use upload object lifetime)
    std::optional<MacComputationContext> context;

    // Clone candidate tracking (clone MAC computations only)
    NodeHandle cloneCandidateHandle;
    std::string cloneCandidateNodeKey;

    MacComputationState(const m_off_t totalSize_,
                        const LocalPath& filePath_,
                        MacComputationThrottle& throttle):
        totalSize(totalSize_),
        filePath(filePath_),
        mThrottle(throttle)
    {}

    ~MacComputationState();

    /**
     * @brief Thread-safe: called by worker thread when chunk MAC is computed.
     */
    void addChunkMacs(chunkmac_map&& chunkMacs, m_off_t newPosition)
    {
        std::lock_guard<std::mutex> g(macsMutex);
        chunkMacs.copyEntriesTo(partialMacs);
        currentPosition = newPosition;
    }

    /**
     * @brief Thread-safe: called by worker thread when local MAC computation completes.
     */
    void setComplete(const int64_t computedLocalMac)
    {
        localMac = computedLocalMac;
        chunkInProgress.store(false, std::memory_order_release);
        completed.store(true, std::memory_order_release);
    }

    /**
     * @brief Thread-safe: called by worker thread on error.
     */
    void setFailed()
    {
        chunkInProgress.store(false, std::memory_order_release);
        failed.store(true, std::memory_order_release);
    }

    /**
     * @brief Thread-safe: check if a chunk is currently being processed.
     */
    bool isChunkInProgress() const
    {
        return chunkInProgress.load(std::memory_order_acquire);
    }

    /**
     * @brief Thread-safe: check if local MAC is ready.
     */
    bool isReady() const
    {
        return completed.load(std::memory_order_acquire);
    }

    /**
     * @brief Thread-safe: check if computation failed.
     */
    bool hasFailed() const
    {
        return failed.load(std::memory_order_acquire);
    }

    /**
     * @brief Thread-safe: check if initialization is complete.
     *
     * Returns true after the initializing thread has finished setting up the computation
     * and the first advanceMacComputation call has returned. This prevents race conditions
     * where checkPendingCloneMac runs before initCloneCandidateMacComputation completes.
     */
    bool isInitializationComplete() const
    {
        return initializationComplete.load(std::memory_order_acquire);
    }

    /**
     * @brief Thread-safe: mark initialization as complete.
     *
     * Called after advanceMacComputation returns in the initialization function.
     */
    void setInitializationComplete()
    {
        initializationComplete.store(true, std::memory_order_release);
    }

    /**
     * @brief Check if stored context matches current state (CSF case only).
     */
    bool contextMatches(const handle currentFsid,
                        const NodeHandle currentCloudHandle,
                        const FileFingerprint& currentLocalFp,
                        const FileFingerprint& currentCloudFp) const
    {
        return context &&
               context->matches(currentFsid, currentCloudHandle, currentLocalFp, currentCloudFp);
    }

private:
    MacComputationThrottle& mThrottle;
};

} // namespace mega

#endif // ENABLE_SYNC
#endif // MEGA_MAC_COMPUTATION_STATE_H
