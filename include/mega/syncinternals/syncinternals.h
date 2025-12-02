/**
 * @file syncinternals.h
 * @brief Class for internal operations of the sync engine.
 */

#ifndef MEGA_SYNCINTERNALS_H
#define MEGA_SYNCINTERNALS_H 1

#ifdef ENABLE_SYNC

#include "mega/db.h"
#include "mega/node.h"

#include <optional>

namespace mega
{

/***************************\
*  FIND LOCAL NODE BY FSID  *
\***************************/

/**
 * @brief Represents the result of a file system ID (FSID) node match operation.
 *
 * This enum provides detailed outcomes of comparing a source node to a target node
 * based on their FSID and associated attributes.
 */
enum class NodeMatchByFSIDResult
{
    /**
     * @brief Nodes are equivalent.
     *
     * The source and target nodes match based on all criteria.
     */
    Matched,

    /**
     * @brief Source and target nodes have different node types.
     *
     * Indicates that the nodes cannot be matched due to type mismatch (e.g., FILENODE vs.
     * FOLDERNODE).
     */
    DifferentTypes,

    /**
     * @brief The source FSID has been reused.
     *
     * Suggests that the source node's parent dir FSID was reused, leading to potential
     * conflicts.
     */
    SourceFsidReused,

    /**
     * @brief Source and target nodes are on different filesystems.
     */
    DifferentFilesystems,

    /**
     * @brief Source and target nodes belong to different owners.
     *
     * We cannot move a node between cloud users (eg inshare to this account, or inshare to
     * inshare), so we avoid detecting those.
     */
    DifferentOwners,

    /**
     * @brief The source node is explicitly excluded from synchronization.
     */
    SourceIsExcluded,

    /**
     * @brief The exclusion state of the source node is unknown.
     */
    SourceExclusionUnknown,

    /**
     * @brief File fingerprints differ.
     *
     * The source and target nodes have mismatching file fingerprints.
     */
    DifferentFingerprint,

    /**
     * @brief File fingerprints differ only in mtime.
     *
     * The source and target nodes have mismatching file fingerprints but only in mtime (CRC, Size
     * and isValid match).
     */
    DifferentFingerprintOnlyMtime,
};

/**
 * @brief Represents the additional attributes needed to match a node by FSID.
 *
 * This structure encapsulates the attributes used to match a node
 * based on its file system ID (FSID) and related properties.
 */
struct NodeMatchByFSIDAttributes
{
    /**
     * @brief The type of the node (e.g., FILENODE, FOLDERNODE).
     */
    nodetype_t nodetype;

    /**
     * @brief The file system fingerprint.
     */
    const fsfp_t& fsfp;

    /**
     * @brief The user handle of the node's owner.
     */
    handle owningUser;

    // In Android we can't set mtime, then we have two fingerprints
    // fingerprint -> expected fingerprint (fingerprint of the file with modified mtime)
    // realFingerprint -> fingerprint from file system
    // In no Android systems, this values should be the same
    /**
     * @brief The file fingerprint for comparison.
     */
    const FileFingerprint& fingerprint;

    /**
     * @brief The real file fingerprint for comparison.
     */
    const FileFingerprint realFingerprint;
};

/**
 * @brief Context for matching source nodes by file system ID.
 *
 * This structure provides contextual information when determining if
 * a file system ID (FSID) has been reused and its exclusion state.
 */
struct SourceNodeMatchByFSIDContext
{
    /**
     * @brief Indicates whether the fsid is reused.
     *
     * true if the fsid has been reused, false otherwise.
     */
    bool isFsidReused;

    /**
     * @brief The exclusion state of the node.
     *
     * Specifies whether the node is included/excluded from syncing.
     */
    ExclusionState exclusionState;
};

/**
 * @brief Indicates whether a LocalNode is part of a scanned or synced context.
 *
 * This is meant to be used to retrieve the corresponding scanned or synced values both
 * for the FSID and the FileFingerprint of the match candidate local node.
 *
 */
enum class ScannedOrSyncedContext
{
    /**
     * @brief SYNCED node context.
     */
    SYNCED,
    /**
     * @brief SCANNED node context.
     */
    SCANNED
};

/**
 * @brief Predicate class for finding a LocalNode by its File System ID (FSID).
 *
 * This class encapsulates the logic needed to determine whether a given LocalNode
 * matches the specified criteria for scanned or synced contexts.

 * It uses areNodesMatchedByFsidEquivalent() to encapsulate filtering logic and validate whether
 * a node is a valid match. Additional checks like node type, owning user, exclusion state,
 * and fingerprints ensure FSID reuse doesn't lead to incorrect matches.
 */
struct FindLocalNodeByFSIDPredicate
{
    /**
     * @brief Constructs the predicate with necessary parameters.
     *
     * @param fsid The FSID to search for.
     * @param scannedOrSyncedType Indicates whether the search is in a synced or scanned context.
     * @param targetNodeAttributes Attributes of the target node to match against.
     * @param originalPathForLogging The original path being processed for context in logs.
     * @param extraCheck Additional optional checks to apply to matching nodes.
     * @param onFingerprintMismatchDuringPutnodes Callback for handling fingerprint mismatches while
     * there are ongoing putnodes operations.
     */
    FindLocalNodeByFSIDPredicate(
        const handle fsid,
        const ScannedOrSyncedContext scannedOrSyncedCtxt,
        const NodeMatchByFSIDAttributes& targetNodeAttributes,
        const LocalPath& originalPathForLogging,
        std::function<bool(const LocalNode&)> extraCheck = nullptr,
        std::function<void(LocalNode*)> onFingerprintMismatchDuringPutnodes = nullptr):

        mFsid{fsid},
        mScannedOrSyncedCtxt{scannedOrSyncedCtxt},
        mTargetNodeAttributes{targetNodeAttributes},
        mOriginalPathForLogging{originalPathForLogging},
        mExtraCheck{extraCheck},
        mOnFingerprintMismatchDuringPutnodes{onFingerprintMismatchDuringPutnodes} {};

    /**
     * @brief Determines if a LocalNode matches the specified criteria.
     *
     * @param localNode The LocalNode to evaluate.
     * @return true if the node matches the criteria, false otherwise.
     */
    bool operator()(LocalNode& localNode);

    /**
     * @brief Resets the early exit condition, preparing the predicate for reuse.
     */
    void resetEarlyExit()
    {
        mEarlyExit = false;
    }

    /**
     * @brief Retrieves the FSID being searched for.
     *
     * @return The FSID handle.
     */
    const handle& fsid() const
    {
        return mFsid;
    }

    /**
     * @brief Indicates if an unknown exclusion was encountered during the search.
     *
     * @return true if an unknown exclusion was found, false otherwise.
     */
    bool foundExclusionUnknown() const
    {
        return mFoundExclusionUnknown;
    }

private:
    /**
     * @brief Checks if the FSID has been reused for the given node.
     *
     * @param localNode The node to check.
     * @return true if the FSID has been reused, false otherwise.
     */
    bool isFsidReused(const LocalNode& localNode) const
    {
        switch (mScannedOrSyncedCtxt)
        {
            case ScannedOrSyncedContext::SYNCED:
                return localNode.fsidSyncedReused;
            case ScannedOrSyncedContext::SCANNED:
                return localNode.fsidScannedReused;
        }
        assert(false && "Unexpected ScannedOrSyncedContext value");
        return localNode.fsidScannedReused; // Fallback to silence compiler warning
    }

    /**
     * @brief Retrieves the fingerprint for the given node.
     *
     * @param localNode The node to retrieve the fingerprint from.
     * @return The file fingerprint of the node.
     */
    const FileFingerprint& getFingerprint(const LocalNode& localNode) const
    {
        switch (mScannedOrSyncedCtxt)
        {
            case ScannedOrSyncedContext::SYNCED:
                return localNode.syncedFingerprint;
            case ScannedOrSyncedContext::SCANNED:
                return localNode.scannedFingerprint;
        }
        assert(false && "Unexpected ScannedOrSyncedContext value");
        return localNode.scannedFingerprint; // Fallback to silence compiler warning
    }

    /**
     * @brief Logs a message with details about the search.
     *
     * @param msg The message to log.
     * @param checkingLocalPath The path of the node being checked.
     */
    void logMsg(const std::string& msg, const LocalPath& checkingLocalPath) const;

    // MEMBERS

    /**
     * @brief The FSID being searched for. This is the primary key for matching two nodes.
     */
    handle mFsid;

    /**
     * @brief Indicates whether the operation is performed in a scanned
     * or synced context.
     */
    ScannedOrSyncedContext mScannedOrSyncedCtxt;

    /**
     * @brief Target node attributes for matching.
     *
     * Encapsulates details such as node type, file system fingerprint,
     * owner, and file fingerprint for the target node being matched.
     */
    const NodeMatchByFSIDAttributes& mTargetNodeAttributes;

    /**
     * @brief Original path of the target node.
     *
     * Provides context for log messages during node matching.
     */
    const LocalPath& mOriginalPathForLogging;

    /**
     * @brief Optional extra check for nodes.
     *
     * A user-defined function for applying additional filtering logic
     * to potential matches.
     */
    std::function<bool(const LocalNode&)> mExtraCheck;

    /**
     * @brief Callback for fingerprint mismatches during ongoing putnodes operations.
     *
     * Optional operation for a LocalNode that
     * has been excluded due to fingerprint mismatch, but the source node has a putnodes operation
     * ongoing for an upload which matches fingerprint with the target node.
     * The param is not const intentionally, in case it needs to be considered as a potential
     * source node, taking into account that there is a fingerprint match for the ongoing upload.
     */
    std::function<void(LocalNode*)> mOnFingerprintMismatchDuringPutnodes;

    /**
     * @brief Flag indicating if an unknown exclusion was encountered.
     */
    bool mFoundExclusionUnknown{false};

    /**
     * @brief Flag for early exit during search.
     *
     * Used to signal an early termination condition in the search loop
     * when certain criteria are met (e.g., mismatch during a putnodes operation detected while
     * meeting mOnFingerprintMismatchDuringPutnodes critera: no need to keep searching for a match).
     */
    bool mEarlyExit{false};
};

/**
 * @brief Determines whether or not a source node and a target node matched by FSID can be
 * considered as equivalent.
 *
 * This method encapsulates the filtering logic for nodes matched by FSID.
 * It checks various properties (e.g., node type, filesystem fingerprint, exclusion state, FSID
 * reuse, file fingerprint) to ensure the node is a valid match. This method is designed for
 * decoupled logic without requiring access to Syncs instance attributes.
 *
 * @param NodeMatchByFSIDAttributes The necessary node attributes of the source node being
 * checked.
 * @param NodeMatchByFSIDAttributes The necessary node attributes of the target node being
 * checked.
 * @param SourceNodeMatchByFSIDContext Additional context of the source node being checked.
 *
 * @return A NodeMatchByFSIDResult enum value with the result.
 *
 * @warning about comparing fingerprints:
 * FSIDs (e.g., inodes on Linux) can be reused when files are deleted and new ones are
 * created. Also when files are updated by replacement.
 * To ensure that we are detecting a true move of the same file (and not incorrectly
 * matching two different files with reused FSIDs), we compare the file fingerprints.
 * The fingerprint provides a heuristic based on file properties like size and
 * modification time. While there is a small chance that a moved file with simultaneous
 * changes could mismatch (causing a reupload), this is far less harmful than mixing two
 * different files and losing data.
 *
 * This check is limited to FILENODE because fingerprints only exists for them.
 * Folder nodes (FOLDERNODE) generally do not have meaningful
 * fingerprints as their state is determined by their contents rather than intrinsic
 * properties.
 * Besides, for folders is much less common to have replacements or delete&create flows,
 * so the FSID alone is usually sufficient for detecting moves.
 */
NodeMatchByFSIDResult areNodesMatchedByFsidEquivalent(const NodeMatchByFSIDAttributes& source,
                                                      const NodeMatchByFSIDAttributes& target,
                                                      const SourceNodeMatchByFSIDContext& context);

/**
 * @brief Finds a LocalNode by its File System ID (FSID) in a specified map.
 *
 * This method matches the provided FSID against cached FSIDs in the given map. It uses
 * FindLocalNodeByFSIDPredicate to encapsulate filtering logic and validate whether
 * a node is a valid match. Additional checks like node type, owning user, exclusion state,
 * and fingerprints ensure FSID reuse doesn't lead to incorrect matches.
 *
 * @param fsidLocalnodeMap The map of FSIDs to LocalNodes.
 * @param FindLocalNodeByFSIDPredicate The predicate with the necessary checks to consider the
 * searched node (already matched by FSID) a valid match, i.e., that the source and target nodes are
 * indeed equivalent.
 *
 * @return a const iterator to the fsid_localnode_map with the matched fsid or to the end if there
 * is no match.
 *
 * @see FindLocalNodeByFSIDPredicate
 */
fsid_localnode_map::const_iterator
    findLocalNodeByFsid_if(const fsid_localnode_map& fsidLocalnodeMap,
                           FindLocalNodeByFSIDPredicate& predicate);

/**
 * @brief Finds a LocalNode by its File System ID (FSID) in a specified map.
 *
 * This method matches the provided FSID against cached FSIDs in the given map. It uses
 * FindLocalNodeByFSIDPredicate to encapsulate filtering logic and validate whether
 * a node is a valid match. Additional checks like node type, owning user, exclusion state,
 * and fingerprints ensure FSID reuse doesn't lead to incorrect matches.
 *
 * @param fsidLocalnodeMap The map of FSIDs to LocalNodes.
 * @param FindLocalNodeByFSIDPredicate The rvalue predicate with the necessary checks to consider
 * the searched node (already matched by FSID) a valid match, i.e., that the source and target nodes
 * are indeed equivalent.
 *
 * @return A pair with:
 *         bool - indicating whether an unknown exclusion was encountered. This may occur during
 * eg. the first pass of the tree after loading from Suspended state and the corresponding node
 * is later in the tree. The caller should decide whether to pospone the logic if an unknown
 * exclusion was found for some node.
 *         LocalNode* - pointer to the matching LocalNode, or nullptr if no match is found.
 *
 * @see FindLocalNodeByFSIDPredicate
 */
std::pair<bool, LocalNode*> findLocalNodeByFsid(const fsid_localnode_map& fsidLocalnodeMap,
                                                FindLocalNodeByFSIDPredicate&& predicate);

/********************************\
*  FIND NODE CANDIDATE TO CLONE  *
\********************************/

/**
 * @brief Finds a suitable node that can be cloned rather than triggering a new upload.
 *
 * This method prepares the local file extension and constructs a predicate to evaluate
 * candidate nodes based on their content and extension. It returns a pointer to a valid
 * clone node if found, or nullptr if no suitable node exists.
 *
 * A valid node to be cloned is a matched node that also has a valid key (no zero-key issue).
 *
 * @param mc Reference to the MegaClient managing the synchronization.
 * @param upload Const reference to the upload task being processed.
 * @return Shared pointer to a valid clone node if found, or nullptr otherwise.
 */
std::shared_ptr<Node> findCloneNodeCandidate(MegaClient& mc,
                                             const SyncUpload_inClient& upload,
                                             const bool excludeMtime);

/****************\
*  SYNC UPLOADS  *
\****************/

/**
 * @brief Manages the upload process for a file, with support for node cloning.
 *
 * This method attempts to find a clone node that matches the local file's content and
 * extension. If a valid node is found, it uses the node for cloning. Otherwise, it
 * proceeds with a normal upload process.
 *
 * @param mc Reference to the MegaClient.
 * @param committer Reference to the transfer database committer.
 * For the other params, see LocalNode::queueClientUpload()
 */
void clientUpload(MegaClient& mc,
                  TransferDbCommitter& committer,
                  std::shared_ptr<SyncUpload_inClient> upload,
                  const VersioningOption vo,
                  const bool queueFirst,
                  const NodeHandle ovHandleIfShortcut);

/******************\
*  SYNC DOWNLOADS  *
\******************/
void clientDownload(MegaClient& mc,
                    TransferDbCommitter& committer,
                    std::shared_ptr<SyncDownload_inClient> download,
                    const bool queueFirst);

/********************\
*  SYNC COMPARISONS  *
\********************/

/**
 * @brief Configurable limits for MAC computation throttling.
 *
 * These values control how many concurrent MAC computations can run
 * and how much total data can be in-flight at once.
 *
 * IMPORTANT: We track CHUNKS in flight, not total file sizes. This allows
 * small files to proceed even when a large file is being processed, since
 * large files only have one chunk in flight at a time.
 *
 * Memory Usage Calculation:
 * - Each file has at most 1 chunk in memory at a time (sync reads, passes to worker)
 * - Max memory = min(maxConcurrentFiles, maxChunksInFlight) × chunkBufferSize
 * - With defaults: min(8, 10) × 10MB = 80MB maximum
 *
 * The chunk buffer also includes padding for cipher block alignment:
 * - Actual allocation = chunkBufferSize + SymmCipher::BLOCKSIZE (16 bytes)
 */
struct MacComputationLimits
{
    // Maximum number of files that can have MAC computation in progress simultaneously
    // Default: 8 files (allows good parallelism without excessive memory)
    // Using uint32_t for fixed size across platforms (32-bit safety)
    uint32_t maxConcurrentFiles = 8;

    // Maximum number of chunks in flight across all files
    // Since each file has at most 1 chunk in flight, this effectively limits memory.
    // Default: 10 chunks (100MB theoretical max, but limited by maxConcurrentFiles to ~80MB)
    uint32_t maxChunksInFlight = 10;

    // Size of each read buffer (matches MacComputationInProgress::BUFFER_SIZE)
    // Each chunk is read, processed for MAC, then released before next chunk.
    // Using int64_t for file sizes (m_off_t may vary by platform)
    static constexpr int64_t chunkBufferSize = 10 * 1024 * 1024; // 10MB

    /**
     * @brief Calculate maximum memory usage for chunk buffers.
     *
     * @return Maximum bytes that could be allocated for chunk buffers.
     */
    constexpr int64_t maxMemoryUsage() const
    {
        // Each file has at most 1 chunk in flight
        const uint32_t effectiveChunks = std::min(maxConcurrentFiles, maxChunksInFlight);
        // Add SymmCipher::BLOCKSIZE (16 bytes) padding per chunk
        return static_cast<int64_t>(effectiveChunks) * (chunkBufferSize + 16);
    }
};

/**
 * @brief Throttle for MAC computation to prevent resource exhaustion.
 *
 * Thread-safe class that tracks and limits concurrent MAC computations.
 * Prevents the sync engine from overwhelming the system with too many
 * simultaneous MAC calculations.
 *
 * IMPORTANT: We track FILES and CHUNKS separately:
 * - Files: Number of files with active MAC computation
 * - Chunks: Number of chunks currently being processed (each ~10MB in memory)
 *
 * This design allows small files to proceed even when large files are being
 * processed, because large files only keep one chunk in memory at a time.
 *
 * Usage:
 * - Call tryAcquireFile() before starting MAC computation for a new file
 * - Call releaseFile() when file computation completes
 * - Call tryAcquireChunk() before queueing a chunk for processing
 * - Call releaseChunk() when chunk processing completes
 */
class MacComputationThrottle
{
public:
    explicit MacComputationThrottle(const MacComputationLimits& limits = {}):
        mLimits(limits)
    {}

    /**
     * @brief Try to acquire a slot for a new file's MAC computation.
     *
     * @return true if slot acquired, false if at file limit
     */
    bool tryAcquireFile()
    {
        std::lock_guard<std::mutex> lock(mMutex);

        if (mCurrentFiles >= mLimits.maxConcurrentFiles)
        {
            return false;
        }

        ++mCurrentFiles;
        return true;
    }

    /**
     * @brief Release a file slot after MAC computation completes.
     */
    void releaseFile()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        assert(mCurrentFiles > 0);
        --mCurrentFiles;
    }

    /**
     * @brief Try to acquire a chunk slot for processing.
     *
     * @return true if slot acquired, false if at chunk limit
     */
    bool tryAcquireChunk()
    {
        std::lock_guard<std::mutex> lock(mMutex);

        if (mCurrentChunks >= mLimits.maxChunksInFlight)
        {
            return false;
        }

        ++mCurrentChunks;
        return true;
    }

    /**
     * @brief Release a chunk slot after processing completes.
     */
    void releaseChunk()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        assert(mCurrentChunks > 0);
        --mCurrentChunks;
    }

    /**
     * @brief Check if a new file could be accepted.
     */
    bool wouldAcceptFile() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mCurrentFiles < mLimits.maxConcurrentFiles;
    }

    /**
     * @brief Check if a new chunk could be accepted.
     */
    bool wouldAcceptChunk() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mCurrentChunks < mLimits.maxChunksInFlight;
    }

    /**
     * @brief Get current number of files being processed.
     */
    uint32_t currentFiles() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mCurrentFiles;
    }

    /**
     * @brief Get current number of chunks in flight.
     */
    uint32_t currentChunks() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mCurrentChunks;
    }

    /**
     * @brief Get the limits configuration.
     */
    const MacComputationLimits& limits() const
    {
        return mLimits;
    }

    /**
     * @brief Update limits (use with caution - may cause temporary over-limit state).
     */
    void setLimits(const MacComputationLimits& limits)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mLimits = limits;
    }

private:
    mutable std::mutex mMutex;
    MacComputationLimits mLimits;
    uint32_t mCurrentFiles = 0;
    uint32_t mCurrentChunks = 0;
};

/**
 * @brief Result type for fingerprint/MAC comparisons.
 *
 * Returns `std::tuple<node_comparison_result, int64_t, int64_t>` where:
 *  - The first element is a `node_comparison_result` indicating:
 *       + NODE_COMP_EARGS: Invalid arguments
 *       + NODE_COMP_EREAD: Error reading the local file.
 *       + NODE_COMP_PENDING: MAC computation initiated but not yet complete (async mode only)
 *       + NODE_COMP_EQUAL: Fingerprints match including mtime
 *       + NODE_COMP_DIFFERS_FP: Node types mismatch or fingerprints differ in something more
 *                               than mtime (CRC, Size, isValid).
 *       + NODE_COMP_DIFFERS_MTIME: Fingerprints differ in mtime but METAMACs match.
 *       + NODE_COMP_DIFFERS_MAC: Fingerprints differ in mtime and METAMACs also differ.
 *  - The second element is the local MetaMAC, or INVALID_META_MAC if not computed.
 *  - The third element is the remote MetaMAC, or INVALID_META_MAC if not computed.
 */
using FsCloudComparisonResult = std::tuple<node_comparison_result, int64_t, int64_t>;

/**
 * @brief Compares fsNode with cloudNode using async MAC computation.
 *
 * For synced files that have a LocalNode. If fingerprints match or differ in more than mtime,
 * returns immediately. If only mtime differs, initiates or checks async MAC computation
 * stored in LocalNode::RareFields.
 *
 * @param syncNode Reference to the LocalNode for storing async MAC computation state.
 * @return Comparison result. Returns NODE_COMP_PENDING if MAC computation in progress.
 *
 * @note METAMACs are only computed if fingerprints differ only in mtime.
 */
FsCloudComparisonResult syncEqualFsCloudExcludingMtimeAsync(MegaClient& mc,
                                                            const CloudNode& cn,
                                                            const FSNode& fs,
                                                            const LocalPath& fsNodeFullPath,
                                                            LocalNode& syncNode);

/**
 * @brief Compares fsNode with cloudNode using blocking MAC computation.
 *
 * For unsynced files without a LocalNode. If fingerprints match or differ in more than mtime,
 * returns immediately. If only mtime differs, performs blocking MAC computation.
 *
 * @return Comparison result. Never returns NODE_COMP_PENDING (blocks until complete).
 *
 * @note METAMACs are only computed if fingerprints differ only in mtime.
 */
FsCloudComparisonResult syncEqualFsCloudExcludingMtimeSync(MegaClient& mc,
                                                           const CloudNode& cn,
                                                           const FSNode& fs,
                                                           const LocalPath& fsNodeFullPath);
} // namespace mega

#endif // ENABLE_SYNC
#endif // MEGA_SYNCINTERNALS_H
