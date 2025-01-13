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

} // namespace mega

#endif // ENABLE_SYNC
#endif // MEGA_SYNCINTERNALS_H
