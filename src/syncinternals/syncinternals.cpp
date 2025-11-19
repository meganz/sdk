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

    if (auto sameFp = source.nodetype != FILENODE || target.fingerprint == source.fingerprint ||
                      target.fingerprint == source.realFingerprint;
        sameFp)
    {
        return NodeMatchByFSIDResult::Matched;
    }

    if (auto sameFpExceptMtime = target.fingerprint.equalExceptMtime(source.fingerprint) ||
                                 target.fingerprint.equalExceptMtime(source.realFingerprint);
        sameFpExceptMtime)
    {
        return NodeMatchByFSIDResult::DifferentFingerprintOnlyMtime;
    }

    return NodeMatchByFSIDResult::DifferentFingerprint;
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

    const auto compRes =
        areNodesMatchedByFsidEquivalent(sourceNodeAttributes, mTargetNodeAttributes, sourceContext);
    switch (compRes)
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
        case NodeMatchByFSIDResult::DifferentFingerprintOnlyMtime:
        {
            if (compRes == NodeMatchByFSIDResult::DifferentFingerprintOnlyMtime)
            {
                LOG_warn << "areNodesMatchedByFsidEquivalent: fingerprint differs only in mtime: "
                         << localNode.getLocalPath().toPath(true);
            }

            if (mOnFingerprintMismatchDuringPutnodes)
            {
                if (const auto upload =
                        std::dynamic_pointer_cast<SyncUpload_inClient>(localNode.transferSP);
                    upload && upload->fingerprint() == mTargetNodeAttributes.fingerprint &&
                    upload->upsyncStarted)
                {
                    // [SDK-5551_TODO]: see this subcase
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
        if (mUpload.mMetaMac.has_value() && mUpload.mMetaMac.value() != INVALID_META_MAC)
        {
            // Avoid calculating metamac again by using precalculated one
            compRes =
                CompareNodeWithProvidedMacAndFpExcludingMtime(&node, mUpload, *mUpload.mMetaMac);
        }
        else
        {
            const auto [auxRes, _] =
                CompareLocalFileWithNodeMacAndFpExludingMtime(mClient,
                                                              mUpload.getLocalname(),
                                                              mUpload,
                                                              &node,
                                                              false /*debugMode*/);

            compRes = auxRes;
        }

        if (compRes == NODE_COMP_EQUAL)
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
        auto node = parent ? mc.childnodebyname(parent.get(), upload->name.c_str()) : nullptr;
        if (node)
        {
            if (auto immediateResult = upload->updateNodeMtime(&mc, node, upload->mtime);
                immediateResult == API_OK)
            {
                // Set `true` even though no actual data transfer occurred, we're updating node's
                // mtime instead
                upload->wasFileTransferCompleted = true;
                upload->upsyncStarted = true;
                return;
            }
            LOG_warn << "clientUpload: UpdateMtime immediate error. Falling back to full upload "
                        "transfer / Cloning node";
        }
        else
        {
            LOG_warn << "clientUpload: "
                     << (!parent ? "Parent Node not found" : "Cloud Node not found")
                     << ". Falling back to full upload transfer / Cloning node";
            assert(false);
        }
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
                    << toNodeHandle(cloneNodeCandidate->nodehandle)
                    << ". Falling back to full upload transfer / Cloning node";
        }
        else
        {
            LOG_err << "fsNode has not changed respect cloudNode but is has been detected as "
                       "changed by sync engine: "
                    << toNodeHandle(cloneNodeCandidate->nodehandle)
                    << ". Falling back to full upload transfer / Cloning node";
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
        if (auto cloudNode = mc.nodeByHandle(download->h); cloudNode)
        {
            if (mc.fsaccess->setmtimelocal(download->getLocalname(), cloudNode->mtime))
            {
                download->wasDistributed = true;
                download->wasFileTransferCompleted.store(true);
                return;
            }

            // [SDK-5551_TODO]: in case of transient error, this could be improved to retry
            // setmtimelocal again before performing download transfer?
            bool transient_err = mc.fsaccess->transient_error;
            LOG_warn << "clientDownload: setmtimelocal failed with ("
                     << (transient_err ? "Transient error" : "Non-transient error") << ")"
                     << ". Falling back to full download transfer.";
        }
        else
        {
            LOG_warn
                << "clientDownload: Cloud node not found. Falling back to full download transfer.";
            assert(false);
        }
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

std::tuple<node_comparison_result, int64_t, int64_t>
    syncEqualFsCloudExcludingMtime(MegaClient& mc,
                                   const CloudNode& cn,
                                   const FSNode& fs,
                                   const LocalPath& fsNodeFullPath)
{
    if (cn.type != fs.type)
        return {NODE_COMP_DIFFERS_FP, INVALID_META_MAC, INVALID_META_MAC};

    if (cn.type != FILENODE)
        return {NODE_COMP_EQUAL, INVALID_META_MAC, INVALID_META_MAC};

    if (!fs.fingerprint.equalExceptMtime(cn.fingerprint))
    {
        if (!fs.fingerprint.isvalid || !cn.fingerprint.isvalid)
        {
            LOG_warn << "syncEqualFsCloudExcludingMtime: fs isValid (" << fs.fingerprint.isvalid
                     << "), cn isValid (" << cn.fingerprint.isvalid << ")";
            assert(fs.fingerprint.isvalid && cn.fingerprint.isvalid);
        }
        return {NODE_COMP_DIFFERS_FP, INVALID_META_MAC, INVALID_META_MAC};
    }

    // IMPORTANT: To avoid performance issues in this method, we will consider FsNode and
    // CloudNode equal if their fingerprints fully match (CRC, size, isValid, mtime), although there
    // could be collisions. We don't want to compute the METAMAC unless strictly necessary because
    // it is expensive in terms of performance, so we will compute it only if the
    // fingerprints differ only in mtime.
    if (fs.fingerprint.mtime == cn.fingerprint.mtime)
        return {NODE_COMP_EQUAL, INVALID_META_MAC, INVALID_META_MAC};

    auto pms =
        std::make_shared<std::promise<std::tuple<node_comparison_result, int64_t, int64_t>>>();
    auto fut = pms->get_future();

    mc.syncs.queueClient(
        [cloudNodeHandle = cn.handle, fsNodeFullPath, pms](MegaClient& mc, TransferDbCommitter&)
        {
            auto node = mc.nodeByHandle(cloudNodeHandle);
            if (!node || node->type != FILENODE || node->nodekey().empty())
            {
                pms->set_value({NODE_COMP_EARGS, INVALID_META_MAC, INVALID_META_MAC});
                return;
            }

            auto fa = mc.fsaccess->newfileaccess();
            if (!fa || !fa->fopen(fsNodeFullPath, true, false, FSLogging::logOnError))
            {
                pms->set_value({NODE_COMP_EREAD, INVALID_META_MAC, INVALID_META_MAC});
                return;
            }

            auto [localMac, remoteMac] =
                genLocalAndRemoteMetaMac(fa.get(), node->nodekey(), node->type);

            auto compRes = localMac == remoteMac ? NODE_COMP_DIFFERS_MTIME : NODE_COMP_DIFFERS_MAC;
            pms->set_value({compRes, localMac, remoteMac});
        });

    return fut.get();
}
} // namespace mega

#endif // ENABLE_SYNC
