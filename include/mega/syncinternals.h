/**
 * @file syncinternals.h
 * @brief Class for internal operations of the sync engine.
 */

#ifndef MEGA_SYNCINTERNALS_H
#define MEGA_SYNCINTERNALS_H 1

#ifdef ENABLE_SYNC

#include "node.h"

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
    DifferentFingerprint
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

    /**
     * @brief The file fingerprint for comparison.
     */
    const FileFingerprint& fingerprint;
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
 * @struct FindCloneNodeCandidatePredicate
 * @brief Predicate for identifying a suitable node candidate to be cloned.
 *
 * This struct encapsulates the logic required to determine if a Node in the cloud
 * matches the content and extension of a local file being uploaded. It is used in
 * conjunction with std::find_if to search for clone candidates in a collection of
 * nodes.
 *
 * @see findCloneNodeCandidate_if
 */
struct FindCloneNodeCandidatePredicate
{
    /**
     * @brief Reference to the MegaClient managing the synchronization process.
     */
    MegaClient& mClient;

    /**
     * @brief Const reference to the shared pointer to the upload task being processed.
     */
    const std::shared_ptr<SyncUpload_inClient>& mUpload;

    /**
     * @brief The file extension of the local file being uploaded.
     */
    const std::string& mLocalExtension;

    /**
     * @brief Function for extracting the file extension from a Node.
     *
     * This function is used to retrieve the extension of cloud nodes being evaluated
     * as potential clone candidates.
     */
    std::function<std::string(const Node& node)> mExtractExtensionFromNode;

    /**
     * @brief Flag indicating if a candidate node with a zero key was found.
     *
     * If true, the predicate encountered a candidate node that matches the local file
     * but has an invalid key, which can be used to prevent it from being cloned.
     */
    bool mFoundCandidateHasZeroKey{false};

    /**
     * @brief Constructs a FindCloneNodeCandidatePredicate instance.
     *
     * @param client Reference to the MegaClient managing synchronization.
     * @param upload Const ref to the shared pointer to the upload task being processed.
     * @param localExtension File extension of the local file being uploaded.
     * @param extractExtensionFromNode Function to extract the file extension from a `Node`.
     */
    FindCloneNodeCandidatePredicate(
        MegaClient& client,
        const std::shared_ptr<SyncUpload_inClient>& upload,
        const std::string& localExtension,
        std::function<std::string(const Node& node)>&& extractExtensionFromNode):
        mClient(client),
        mUpload(upload),
        mLocalExtension(localExtension),
        mExtractExtensionFromNode(std::move(extractExtensionFromNode))
    {}

    /**
     * @brief Evaluates if the provided Node is a suitable clone candidate.
     *
     * Checks if the node matches the local file in terms of content and extension.
     * If a match is found but the node has a zero key, it returns true but it logs a warning
     * and updates the mFoundCandidateHasZeroKey flag accordingly.
     *
     * @param node The cloud node to evaluate.
     * @return True if a match is found, otherwise false.
     */
    bool operator()(const Node& node);
};

/**
 * @brief Searches for a suitable clone node in the provided candidates using a predicate.
 *
 * This function iterates through a collection of potential nodes and evaluates each
 * using the provided predicate. It returns an iterator to the first node that satisfies
 * the predicate or the end iterator if no match is found.
 *
 * @param candidates A sharedNode_vector of candidate nodes.
 * @param predicate The predicate used to evaluate each candidate node.
 * @return Iterator to the first node that satisfies the predicate, or the end iterator.
 *
 * @see FindCloneNodeCandidatePredicate
 */
sharedNode_vector::const_iterator
    findCloneNodeCandidate_if(const sharedNode_vector& candidates,
                              FindCloneNodeCandidatePredicate& predicate);

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
 * @param upload Shared pointer to the upload task being processed.
 * @return Pointer to a valid clone node if found, or nullptr otherwise.
 *
 * @see findCloneNodeCandidate_if
 * @see FindCloneNodeCandidatePredicate
 */
Node* findCloneNodeCandidate(MegaClient& mc, const std::shared_ptr<SyncUpload_inClient>& upload);

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
 * @param upload Const ref to the shared pointer to the SyncUpload to be processed.
 * @param vo Versioning option for the upload.
 * @param queueFirst Flag indicating if this upload should be prioritized.
 * @param ovHandleIfShortcut Node handle representing a shortcut for the upload.
 */
void clientUpload(MegaClient& mc,
                  TransferDbCommitter& committer,
                  const shared_ptr<SyncUpload_inClient>& upload,
                  const VersioningOption vo,
                  const bool queueFirst,
                  const NodeHandle ovHandleIfShortcut);

struct ThrottleValueLimits
{
    unsigned throttleUpdateRateLowerLimit;
    unsigned throttleUpdateRateUpperLimit;
    unsigned maxUploadsBeforeThrottleLowerLimit;
    unsigned maxUploadsBeforeThrottleUpperLimit;
};

/**
 * @class IUploadThrottlingManager
 * @brief Interface for the manager in charge of throttling and delayed processing of uploads.
 *
 * The IUploadThrottlingManager is meant to handle the queuing and processing of delayed uploads,
 * including the throttling time and the max number of uploads allowed for a file before throttle.
 */
class IUploadThrottlingManager
{
    /**
     * @brief Checks if the next delayed upload in the queue should be processed.
     *
     * @return True if the next upload should be processed, otherwise false.
     */
    virtual bool checkProcessDelayedUploads() const = 0;

    /**
     * @brief Resets last processed time to the current time.
     */
    virtual void resetLastProcessedTime() = 0;

public:
    /**
     * @brief IUploadThrottlingManager destructor.
     */
    virtual ~IUploadThrottlingManager() = default;

    /**
     * @brief Adds a delayed upload to be processed.
     * @param The upload to be throttled and processed.
     */
    virtual void addToDelayedUploads(DelayedSyncUpload&& /* delayedUpload */) = 0;

    /**
     * @brief Processes the delayed uploads.
     *
     * Calls completion function if there a DelayedUpload was processed.
     */
    virtual void processDelayedUploads(
        std::function<void(std::weak_ptr<SyncUpload_inClient>&& upload,
                           const VersioningOption vo,
                           const bool queueFirst,
                           const NodeHandle ovHandleIfShortcut)>&& /* completion */) = 0;

    // Setters

    /**
     * @brief Sets the throttle update rate in seconds.
     * @param The throttle update rate in seconds.
     */
    virtual bool setThrottleUpdateRate(const unsigned /* intervalSeconds */) = 0;

    /**
     * @brief Sets the throttle update rate as a duration.
     * @param The interval as a std::chrono::seconds object.
     */
    virtual bool setThrottleUpdateRate(const std::chrono::seconds /* interval */) = 0;

    /**
     * @brief Sets the maximum uploads allowed before throttling.
     * @param The maximum number of uploads that will be uploaded unthrottled.
     */
    virtual bool setMaxUploadsBeforeThrottle(const unsigned /* maxUploadsBeforeThrottle */) = 0;

    // Getters

    /**
     * @brief Gets the upload counter inactivity expiration time.
     * @return The expiration time as a std::chrono::seconds object.
     */
    virtual std::chrono::seconds uploadCounterInactivityExpirationTime() const = 0;

    /**
     * @brief Gets the throttle update rate for uploads in seconds.
     * @return The throttle update rate in seconds.
     */
    virtual unsigned throttleUpdateRate() const = 0;

    /**
     * @brief Gets the maximum uploads allowed before throttling.
     * @return The maximum number of uploads.
     */
    virtual unsigned maxUploadsBeforeThrottle() const = 0;

    /**
     * @brief Gets the lower and upper limits for throttling values.
     * @return The ThrottleValueLimits struct with lower and upper limits.
     */
    virtual ThrottleValueLimits throttleValueLimits() const = 0;
};

/**
 * @class UploadThrottlingManager
 * @brief Manages throttling and delayed processing of uploads.
 *
 * The UploadThrottlingManager handles the queuing and processing of delayed uploads,
 * including the throttling time and the max number of uploads allowed for a file before throttle.
 * It adjusts the throttle update rate dynamically based on queue size, allowing for
 * efficient upload handling without overloading system resources. Configuration
 * options allow users to tune the behavior as per their requirements.
 */
class UploadThrottlingManager: public IUploadThrottlingManager
{
public:
    /**
     * @brief Adds a delayed upload to the delayed queue.
     * @param delayedUpload The upload to be added to the delayed queue.
     */
    void addToDelayedUploads(DelayedSyncUpload&& delayedUpload) override
    {
        mDelayedQueue.emplace(std::move(delayedUpload));
    }

    /**
     * @brief Processes the delayed upload queue.
     *
     * Processes the next delayed upload in the queue, ensuring that throttling conditions
     * are met before initiating uploads.
     *
     * If the next delayed upload is not valid (DelayedSyncUpload::weakUpload is not valid), it will
     * be skipped and the next delayed upload in the queue, if any, will be the one to be processed.
     *
     * If a valid delayed upload is processed, it will be passed to the completion function for
     * futher processing (ex: enqueue the upload to the client)
     *
     * @see checkProcessDelayedUploads()
     */
    void processDelayedUploads(
        std::function<void(std::weak_ptr<SyncUpload_inClient>&& upload,
                           const VersioningOption vo,
                           const bool queueFirst,
                           const NodeHandle ovHandleIfShortcut)>&& completion) override;

    // Setters

    /**
     * @brief Sets the throttle update rate in seconds.
     * @param intervalSeconds The interval in seconds. It cannot be below
     * THROTTLE_UPDATE_RATE_LOWER_LIMIT nor above THROTTLE_UPDATE_RATE_UPPER_LIMIT.
     */
    bool setThrottleUpdateRate(const unsigned intervalSeconds) override
    {
        return setThrottleUpdateRate(std::chrono::seconds(intervalSeconds));
    }

    /**
     * @brief Sets the throttle update rate as a duration.
     * @param interval The interval as a std::chrono::seconds object.
     */
    bool setThrottleUpdateRate(const std::chrono::seconds interval) override;

    /**
     * @brief Sets the maximum uploads allowed before throttling.
     * @param maxUploadsBeforeThrottle The maximum number of uploads that will be uploaded
     * unthrottled. It cannot be below MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT nor above
     * MAX_UPLOADS_BEFORE_THROTTLE_UPPER_LIMIT.
     */
    bool setMaxUploadsBeforeThrottle(const unsigned maxUploadsBeforeThrottle) override;

    // Getters

    /**
     * @brief Gets the upload counter inactivity expiration time.
     * @return The expiration time as a std::chrono::seconds object.
     */
    std::chrono::seconds uploadCounterInactivityExpirationTime() const override
    {
        return mUploadCounterInactivityExpirationTime;
    }

    /**
     * @brief Gets the throttle update rate for uploads in seconds.
     * @return The throttle update rate in seconds.
     */
    unsigned throttleUpdateRate() const override
    {
        return static_cast<unsigned>(mThrottleUpdateRate.count());
    }

    /**
     * @brief Gets the maximum uploads allowed before throttling.
     * @return The maximum number of uploads.
     */
    unsigned maxUploadsBeforeThrottle() const override
    {
        return mMaxUploadsBeforeThrottle;
    }

    /**
     * @brief Gets the lower and upper limits for throttling values.
     * @return The ThrottleValueLimits struct with lower and upper limits.
     */
    ThrottleValueLimits throttleValueLimits() const override
    {
        return {THROTTLE_UPDATE_RATE_LOWER_LIMIT,
                THROTTLE_UPDATE_RATE_UPPER_LIMIT,
                MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT,
                MAX_UPLOADS_BEFORE_THROTTLE_UPPER_LIMIT};
    }

private:
    // Limits
    static constexpr unsigned TIMEOUT_TO_RESET_UPLOAD_COUNTERS_SECONDS{
        86400}; // Timeout (in seconds) to reset upload counters due to inactivity.
    static constexpr unsigned THROTTLE_UPDATE_RATE_LOWER_LIMIT{
        60}; // Minimum allowed interval for processing delayed uploads.
    static constexpr unsigned THROTTLE_UPDATE_RATE_UPPER_LIMIT{
        TIMEOUT_TO_RESET_UPLOAD_COUNTERS_SECONDS - 1}; // Maximum allowed interval for processing
                                                       // delayed uploads.
    static constexpr unsigned MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT{
        2}; // Minimum allowed of max uploads before throttle.
    static constexpr unsigned MAX_UPLOADS_BEFORE_THROTTLE_UPPER_LIMIT{
        5}; // Maximum allowed of max uploads before throttle.

    // Default values
    static constexpr unsigned DEFAULT_PROCESS_INTERVAL_SECONDS{
        180}; // Default interval (in seconds) for processing delayed uploads.
    static constexpr unsigned DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE{
        MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT}; // Default maximum uploads allowed before
                                                  // throttling.

    // Members
    std::queue<DelayedSyncUpload> mDelayedQueue; // Queue of delayed uploads to be processed.
    std::chrono::steady_clock::time_point mLastProcessedTime{
        std::chrono::steady_clock::now()}; // Timestamp of the last processed upload.
    std::chrono::seconds mUploadCounterInactivityExpirationTime{
        TIMEOUT_TO_RESET_UPLOAD_COUNTERS_SECONDS}; // Timeout for resetting upload counters due to
                                                   // inactivity.

    // Configurable members
    std::chrono::seconds mThrottleUpdateRate{
        DEFAULT_PROCESS_INTERVAL_SECONDS}; // Configurable interval for processing uploads.
    unsigned mMaxUploadsBeforeThrottle{
        DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE}; // Maximum uploads allowed before throttling.

    /**
     * @brief Checks if the next delayed upload in the queue should be processed.
     *
     * Calculates a dynamic update rate taking into account:
     *    1. mDelayedQueue size.
     *    2. mThrottleUpdateRate (reference value).
     *    3. THROTTLE_UPDATE_RATE_LOWER_LIMIT.
     * The dynamic rate is the max between the THROTTLE_UPDATE_RATE_LOWER_LIMIT and the result of
     * mThrottleUpdateRate / sqrt(mDelayedQueue.size())
     *
     * @return True if the next upload should be processed, otherwise false.
     */
    bool checkProcessDelayedUploads() const override;

    /**
     * @brief Resets last processed time to the current time.
     */
    void resetLastProcessedTime() override
    {
        mLastProcessedTime = std::chrono::steady_clock::now();
    }
};

} // namespace mega

#endif // ENABLE_SYNC
#endif // MEGA_SYNCINTERNALS_H
