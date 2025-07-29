/**
 * @file syncinternals.cpp
 * @brief Class for internal operations of the sync engine.
 */

#ifdef ENABLE_SYNC

#include "mega/syncinternals/syncinternals.h"

#include "mega/megaclient.h"
#include "mega/sync.h"
#include "mega/syncinternals/syncinternals_logging.h"

#include <memory>

namespace mega
{

/***************************\
*  FIND LOCAL NODE BY FSID  *
\***************************/

NodeMatchByFSIDResult
    areNodesMatchedByFsidEquivalent(const NodeMatchByFSIDAttributes& source,
                                    const NodeMatchByFSIDAttributes& target,
                                    const SourceNodeMatchByFSIDContext& sourceContext)
{
    if (source.nodetype != target.nodetype)
        return NodeMatchByFSIDResult::DifferentTypes;

    if (sourceContext.isFsidReused)
        return NodeMatchByFSIDResult::SourceFsidReused;

    if (source.fsfp != target.fsfp)
        return NodeMatchByFSIDResult::DifferentFilesystems;

    if (target.owningUser != UNDEF && source.owningUser != target.owningUser)
        return NodeMatchByFSIDResult::DifferentOwners;

    switch (sourceContext.exclusionState)
    {
        case ES_INCLUDED:
            break;
        case ES_UNKNOWN:
            return NodeMatchByFSIDResult::SourceExclusionUnknown;
        case ES_EXCLUDED:
            return NodeMatchByFSIDResult::SourceIsExcluded;
        default:
            assert(false && "Invalid exclusion state for source node");
            return NodeMatchByFSIDResult::SourceIsExcluded; // Default to exclusion on invalid state
    }

    // IMPORTANT: Ensure that we are not mixing two different files whose FSIDs have been reused.
    if (source.nodetype == FILENODE && target.fingerprint != source.fingerprint &&
        target.fingerprint != source.realFingerprint)
        return NodeMatchByFSIDResult::DifferentFingerprint;

    return NodeMatchByFSIDResult::Matched;
}

bool FindLocalNodeByFSIDPredicate::operator()(LocalNode& localNode)
{
    if (mEarlyExit)
        return false;

    const NodeMatchByFSIDAttributes sourceNodeAttributes{
        localNode.type,
        localNode.sync->fsfp(),
        localNode.sync->cloudRootOwningUser,
        getFingerprint(localNode),
        (mScannedOrSyncedCtxt == ScannedOrSyncedContext::SCANNED) ?
            localNode.realScannedFingerprint :
            localNode.syncedFingerprint};
    const SourceNodeMatchByFSIDContext sourceContext{isFsidReused(localNode),
                                                     localNode.exclusionState()};

    switch (
        areNodesMatchedByFsidEquivalent(sourceNodeAttributes, mTargetNodeAttributes, sourceContext))
    {
        case NodeMatchByFSIDResult::Matched:
        {
            if (!mExtraCheck || mExtraCheck(localNode))
            {
                logMsg("found", localNode.getLocalPath());
                return true;
            }
            return false;
        }
        case NodeMatchByFSIDResult::SourceExclusionUnknown:
        {
            mFoundExclusionUnknown = true;
            logMsg("unknown exclusion with that fsid", localNode.getLocalPath());
            return false;
        }
        case NodeMatchByFSIDResult::DifferentFingerprint:
        {
            if (mOnFingerprintMismatchDuringPutnodes)
            {
                if (const auto upload =
                        std::dynamic_pointer_cast<SyncUpload_inClient>(localNode.transferSP);
                    upload && upload->fingerprint() == mTargetNodeAttributes.fingerprint &&
                    upload->putnodesStarted)
                {
                    logMsg("source with same fsid excluded due to fingerprint mismatch has "
                           "a putnodes operation ongoing, fsid",
                           localNode.getLocalPath());
                    // Excluded source node has a putnodes operation in flight
                    mOnFingerprintMismatchDuringPutnodes(&localNode);
                    mEarlyExit = true;
                }
            }
            return false;
        }
        default:
            return false;
    }
}

void FindLocalNodeByFSIDPredicate::logMsg(const std::string& msg,
                                          const LocalPath& checkingLocalPath) const
{
    const std::string prefix = mScannedOrSyncedCtxt == ScannedOrSyncedContext::SYNCED ?
                                   "findLocalNodeBySyncedFsid" :
                                   "findLocalNodeByScannedFsid";
    LOG_verbose << prefix << " - " << msg << " " << toHandle(mFsid) << " at: " << checkingLocalPath
                << " checked from " << mOriginalPathForLogging;
}

// Finds a LocalNode by its File System ID (FSID) in a specified map.
// The functionality is documented for the public method findLocalNodeByFsid()
fsid_localnode_map::const_iterator
    findLocalNodeByFsid_if(const fsid_localnode_map& fsidLocalnodeMap,
                           FindLocalNodeByFSIDPredicate& predicate)
{
    if (predicate.fsid() == UNDEF)
    {
        LOG_debug << " - FSID is undef, skipping";
        return std::end(fsidLocalnodeMap);
    }

    // Iterate over all nodes with the given FSID
    const auto [begin, end] = fsidLocalnodeMap.equal_range(predicate.fsid());
    const auto it = std::find_if(begin,
                                 end,
                                 [&predicate](const auto& pair) -> bool
                                 {
                                     const auto& localNode = pair.second;
                                     if (!localNode || !localNode->sync)
                                     {
                                         assert(false && "Invalid LocalNode or its sync");
                                         return false;
                                     }
                                     return predicate(*localNode);
                                 });
    predicate.resetEarlyExit();
    return it == end ? std::end(fsidLocalnodeMap) : it;
}

std::pair<bool, LocalNode*> findLocalNodeByFsid(const fsid_localnode_map& fsidLocalnodeMap,
                                                FindLocalNodeByFSIDPredicate&& predicate)
{
    const auto it = findLocalNodeByFsid_if(fsidLocalnodeMap, predicate);
    const auto& matchedNodePtr = it != std::end(fsidLocalnodeMap) ? it->second : nullptr;

    return {predicate.foundExclusionUnknown(), matchedNodePtr};
}

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
     * @brief Const reference to the upload task being processed.
     */
    const SyncUpload_inClient& mUpload;

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
        const SyncUpload_inClient& upload,
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
    bool operator()(const Node& node)
    {
        const std::string cloudExtension = mExtractExtensionFromNode(node);
        if (mClient.treatAsIfFileDataEqual(node, mLocalExtension, mUpload, cloudExtension))
        {
            // Found a candidate that matches content
            if (node.hasZeroKey())
            {
                LOG_warn << "Clone node key is a zero key!! Avoid cloning node [path = '"
                         << node.displaypath() << "', sourceLocalname = '"
                         << mUpload.sourceLocalname << "']";
                mClient.sendevent(99486, "Node has a zerokey");
                mFoundCandidateHasZeroKey = true;
            }
            return true; // Done searching (zero key or valid node)
        }
        return false; // keep searching
    }

    /**
     * @brief Overloaded operator() to check the validity of a shared_ptr to a Node.
     */
    bool operator()(const std::shared_ptr<Node>& node)
    {
        return node && operator()(*node);
    }
};

Node* findCloneNodeCandidate(MegaClient& mc, const SyncUpload_inClient& upload)
{
    // Prepare the local extension.
    const auto extLocal = [](const LocalPath& localPath) -> std::string
    {
        std::string extension;
        FileSystemAccess::getextension(localPath, extension);
        return removeDot(std::move(extension));
    }(upload.getLocalname());

    // Helper for node-based extension.
    auto extractExtensionFromNode = [](const Node& node) -> std::string
    {
        std::string extension;
        node.getExtension(extension, node.displayname());
        return removeDot(std::move(extension));
    };

    FindCloneNodeCandidatePredicate predicate{mc,
                                              upload,
                                              extLocal,
                                              std::move(extractExtensionFromNode)};

    const auto candidates{mc.mNodeManager.getNodesByFingerprint(upload)};

    if (const auto it = std::find_if(begin(candidates), end(candidates), predicate);
        (it != std::end(candidates) && !predicate.mFoundCandidateHasZeroKey))
        return it->get();

    return nullptr;
}

/****************\
*  SYNC UPLOADS  *
\****************/

void clientUpload(MegaClient& mc,
                  TransferDbCommitter& committer,
                  std::shared_ptr<SyncUpload_inClient> upload,
                  const VersioningOption vo,
                  const bool queueFirst,
                  const NodeHandle ovHandleIfShortcut)
{
    assert(!upload->wasStarted);
    upload->wasStarted = true;

    // If we found a node to clone with a valid key, call putNodesToCloneNode.
    if (auto cloneNode = findCloneNodeCandidate(mc, *upload); cloneNode)
    {
        const auto displayPath = cloneNode->displaypath();
        LOG_debug << "Cloning node rather than sync uploading: " << displayPath << " for "
                  << upload->sourceLocalname;

        // completion function is supplied to putNodes command
        upload->sendPutnodesToCloneNode(&mc, ovHandleIfShortcut, cloneNode);
        upload->putnodesStarted = true;
        upload->wasCompleted = true;
        return;
    }

    // Trying to optimize upload
    if (auto fa = mc.fsaccess->newfileaccess(); fa->fopen(upload->sourceLocalname, true, false, FSLogging::logOnError))
    {
        auto tempTransfer = std::make_unique<Transfer>(&mc, PUT);
        tempTransfer->localfilename = upload->sourceLocalname;
        tempTransfer->genfingerprint(fa.get());
        if (tempTransfer->tryOptimizedUpload(committer))
        {
            LOG_debug << "Sync upload optimized by file copy";
            upload->putnodesStarted = true;
            upload->wasCompleted = true;
            return;
        }
    }

    // Otherwise, proceed with the normal upload.
    upload->tag = mc.nextreqtag();
    upload->selfKeepAlive = upload;
    mc.startxfer(PUT, upload.get(), committer, false, queueFirst, false, vo, nullptr, upload->tag);
}

} // namespace mega

#endif // ENABLE_SYNC
