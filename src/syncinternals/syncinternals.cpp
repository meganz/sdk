/**
 * @file syncinternals.cpp
 * @brief Class for internal operations of the sync engine.
 */

#include "mega/base64.h"
#ifdef ENABLE_SYNC

#include "mega/syncinternals/syncinternals.h"

#include "mega/megaclient.h"
#include "mega/sync.h"
#include "mega/syncinternals/syncinternals_logging.h"
#include "mega/utils.h"

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
                    upload->upsyncStarted)
                {
                    // [TO_DO]: see this subcase (sdk-5551)
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
 * matches the Fingerprint and Meta Mac of a local file being uploaded. It is used in
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
     */
    FindCloneNodeCandidatePredicate(MegaClient& client, const SyncUpload_inClient& upload):
        mClient(client),
        mUpload(upload)
    {}

    /**
     * @brief Evaluates if the provided Node is a suitable clone candidate.
     *
     * Checks if the node matches the local file in terms of Fingerprint and Meta Mac.
     * If a match is found but the node has a zero key, it returns true but it logs a warning
     * and updates the mFoundCandidateHasZeroKey flag accordingly.
     *
     * @param node The cloud node to evaluate.
     * @return True if a match is found, otherwise false.
     */
    bool operator()(const Node& node)
    {
        node_comparison_result compRes = NODE_COMP_EQUAL;
        if (mUpload.mMetaMac.has_value())
        {
            // Avoid calculating metamac again by using precalculated one
            compRes = CompareLocalFileWithNodeFpAndPrecalculatedMac(mUpload,
                                                                    &node,
                                                                    *mUpload.mMetaMac,
                                                                    true /*excludeMtime*/)
        }
        else
        {
            const auto [auxCompRes, _] = CompareLocalFileWithNodeFpAndMac(mClient,
                                                                          mUpload.getLocalname(),
                                                                          mUpload,
                                                                          &node,
                                                                          true /*excludeMtime*/,
                                                                          false /*debugMode*/);
            compRes = auxCompRes;
        }

        if (compRes == NODE_COMP_EQUAL || compRes == NODE_COMP_DIFFERS_MTIME)
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

std::shared_ptr<Node> findCloneNodeCandidate(MegaClient& mc,
                                             const SyncUpload_inClient& upload,
                                             const bool excludeMtime)
{
    FindCloneNodeCandidatePredicate predicate{
        mc,
        upload,
    };
    std::vector<std::shared_ptr<Node>> matches;
    const auto candidates{mc.mNodeManager.getNodesByFingerprint(upload, excludeMtime)};

    std::copy_if(candidates.begin(),
                 candidates.end(),
                 std::back_inserter(matches),
                 [&](const auto& nodePtr)
                 {
                     return predicate(nodePtr) && !predicate.mFoundCandidateHasZeroKey;
                 });

    if (matches.empty())
        return nullptr;

    // All nodes in `matches` vector have the same fingerprint and META_MAC than upload node
    std::shared_ptr<Node> candidateNode = matches.at(0);
    for (auto& n: matches)
    {
        // Break if we find a candidate in `matches` vector with same name and target node
        if (n->displayname() == upload.name && n->parentHandle() == upload.h)
        {
            candidateNode = n;
            break;
        }
    }
    return candidateNode;
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

    if (upload->wasJustMtimeChanged)
    {
        auto parent = mc.nodeByHandle(upload->h);
        if (!parent)
        {
            LOG_warn << "clientUpload: Parent Node not found";
            upload->upsyncFailed = true;
            upload->wasUpsyncCompleted.store(true);
            return;
        }

        auto node = mc.childnodebyname(parent.get(), upload->name.c_str());
        if (!node)
        {
            LOG_warn << "clientUpload: Target Node not found";
            upload->upsyncFailed = true;
            upload->wasUpsyncCompleted.store(true);
            return;
        }

        upload->updateNodeMtime(&mc, node, upload->mtime);
        // Set `true` even though no actual data transfer occurred, we're updating node's mtime
        // instead
        upload->wasFileTransferCompleted = true;
        upload->upsyncStarted = true;
        return;
    }

    auto cloneNodeCandidate = findCloneNodeCandidate(mc, *upload, true /*excludeMtime*/);
    if (!cloneNodeCandidate)
    {
        // Otherwise, proceed with the normal upload.
        upload->tag = mc.nextreqtag();
        upload->selfKeepAlive = upload;
        mc.startxfer(PUT,
                     upload.get(),
                     committer,
                     false,
                     queueFirst,
                     false,
                     vo,
                     nullptr,
                     upload->tag);
        return;
    }

    if (auto isSameNode = cloneNodeCandidate->displayname() == upload->name &&
                          cloneNodeCandidate->parentHandle() == upload->h;
        isSameNode)
    {
        if (cloneNodeCandidate->mtime != upload->mtime)
        {
            LOG_err << "fsNode has changed just mtime respect cloudNode and it should have managed "
                       "before: "
                    << toNodeHandle(cloneNodeCandidate->nodehandle);
            assert(false && "fsNode has not changed respect cloudNode");
        }
        else
        {
            // Fallback to cloning node
            // This should not happen as candidate node has same name META_MAC and FP (including
            // mtime) than upload node It means that node has not changed but it has been detected
            // as changed
            LOG_err << "fsNode has not changed respect cloudNode but is was detected as changed by "
                       "sync engine: "
                    << toNodeHandle(cloneNodeCandidate->nodehandle);
            assert(false && "fsNode has not changed respect cloudNode");
        }
    }

    // We have found a candidate node to clone with a valid key, call putNodesToCloneNode.
    const auto displayPath = cloneNodeCandidate->displaypath();
    LOG_debug << "Cloning node rather than sync uploading: " << displayPath << " for "
              << upload->sourceLocalname;

    // completion function is supplied to putNodes command
    upload->sendPutnodesToCloneNode(&mc, ovHandleIfShortcut, cloneNodeCandidate.get());
    // Set `true` even though no actual data transfer occurred, we're sending putnodes to clone
    // node instead
    upload->wasFileTransferCompleted = true;
    upload->upsyncStarted = true;
    return;
}

/******************\
*  SYNC DOWNLOADS  *
\******************/
void clientDownload(MegaClient& mc,
                    TransferDbCommitter& committer,
                    std::shared_ptr<SyncDownload_inClient> download,
                    const bool queueFirst)
{
    if (download->wasJustMtimeChanged)
    {
        auto cloudNode = mc.nodeByHandle(download->h);
        if (!cloudNode)
        {
            LOG_warn << "clientDownload: Cloud Node not found";
            download->wasDistributed = true;
            download->wasFileTransferCompleted.store(false);
            return;
        }

        if (auto success = mc.fsaccess->setmtimelocal(download->getLocalname(), cloudNode->mtime);
            success)
        {
            download->wasDistributed = true;
            download->wasFileTransferCompleted.store(true);
            return;
        }

        bool transient_err = mc.fsaccess->transient_error;
        LOG_warn << "clientDownload: setmtimelocal failed with ("
                 << (transient_err ? "Transient error" : "Non-transient error") << ")"
                 << ". Falling back to full download transfer";

        // [TO_CHECK]: in case of transient error, this could be improved to retry setmtimelocal
        // again before performing download trasnsfer?
    }

    // Proceed with the download transfer.
    mc.startxfer(GET,
                 download.get(),
                 committer,
                 false,
                 queueFirst,
                 false,
                 NoVersioning,
                 nullptr,
                 mc.nextreqtag());
}

std::pair<node_comparison_result, int64_t>
    syncCompCloudToFsWithMac_internal(MegaClient& mc,
                                      const CloudNode& cn,
                                      const FSNode& fs,
                                      const LocalPath& fsNodeFullPath,
                                      const bool excludeMtime)
{
    auto node = mc.nodeByHandle(cn.handle);
    if (!node)
    {
        // [TO_CHECK]. What is expected in case Node cannot be retrieved from CloudNode?
        return {NODE_COMP_EARGS, 0};
    }

    return CompareLocalFileWithNodeFpAndMac(mc,
                                            fsNodeFullPath,
                                            fs.fingerprint,
                                            node.get(),
                                            excludeMtime);
}

std::pair<node_comparison_result, int64_t> syncCompCloudToFsWithMac(MegaClient& mc,
                                                                    const CloudNode& cn,
                                                                    const FSNode& fs,
                                                                    const LocalPath& fsNodeFullPath,
                                                                    const bool excludeMtime)
{
    if (cn.type != fs.type)
        return {NODE_COMP_INVALID_NODE_TYPE, 0};
    if (cn.type != FILENODE)
        return {NODE_COMP_EQUAL, 0};
    assert(cn.fingerprint.isvalid && fs.fingerprint.isvalid);
    return syncCompCloudToFsWithMac_internal(mc, cn, fs, fsNodeFullPath, excludeMtime);
}
} // namespace mega

#endif // ENABLE_SYNC
