/**
 * @file syncinternals.cpp
 * @brief Class for internal operations of the sync engine.
 */

#include "mega/base64.h"
#ifdef ENABLE_SYNC

#include "mega/megaclient.h"
#include "mega/sync.h"
#include "mega/syncinternals/syncinternals.h"
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
     * Checks if the node matches the local file in terms of Fingerprint, and Meta Mac if
     * fingerprints only differs in mtime. If a match is found but the node has a zero key, it
     * returns true but it logs a warning and updates the mFoundCandidateHasZeroKey flag
     * accordingly.
     *
     * @param node The cloud node to evaluate.
     * @return True if a match is found, otherwise false.
     */
    bool operator()(const Node& node)
    {
        static const std::string logPre{"FindCloneCandidate: "};
        const auto nodePath = node.displayname();
        node_comparison_result compRes = NODE_COMP_EQUAL;
        std::string compResStr;
        if (mUpload.mMetaMac.has_value() && mUpload.mMetaMac.value() != INVALID_META_MAC)
        {
            // Avoid calculating metamac again by using precalculated one
            compRes =
                CompareNodeWithProvidedMacAndFpExcludingMtime(&node, mUpload, *mUpload.mMetaMac);
            compResStr = nodeComparisonResultToStr(compRes);
            LOG_debug << logPre
                      << "CompareNodeWithProvidedMacAndFpExcludingMtime res: " << compResStr
                      << " [path = " << nodePath << "]";
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
            compResStr = nodeComparisonResultToStr(compRes);
            LOG_debug << logPre
                      << "CompareLocalFileWithNodeMacAndFpExcludingMtime res: " << compResStr
                      << " [path = " << nodePath << "]";
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
            LOG_debug << logPre << compResStr << " -> return true [path = " << nodePath << "]";
            return true; // Done searching (zero key or valid node)
        }
        LOG_debug << logPre << compResStr << " -> return false [path = " << nodePath << "]";
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
    auto mtimeCompletion = [&mc,
                            vo,
                            queueFirst,
                            ovHandleIfShortcut,
                            uploadWptr = upload->weak_from_this()](NodeHandle h, Error e)
    {
        if (auto u = uploadWptr.lock())
        {
            // Set `upSyncFailed` to `False` regardless of settAttr result.
            // - If setAttr succeeded => upSyncFailed must be `False`
            // - If setAttr failed we want to execute fallback mechanism (Full upload or Clone
            // Node), so we need to reset flag anyway
            u->wasJustMtimeChanged = false;
            u->upsyncFailed = false;

            if (auto setMtimeSucceeded = e == API_OK; setMtimeSucceeded)
            {
                // Let the engine know the setAttr has been completed successfully.
                u->upsyncResultHandle = h;
                u->wasUpsyncCompleted.store(true);
                return;
            }

            if (auto cloneNodeCandidate = findCloneNodeCandidate(mc, *u, true /*excludeMtime*/);
                cloneNodeCandidate)
            {
                LOG_err << "clientUpload (Update mTime): Error(" << e << "), Node("
                        << toNodeHandle(h) << "). Falling back to Cloning node";
                u->cloneNode(mc, cloneNodeCandidate, ovHandleIfShortcut);
                return;
            }

            // We need to queue full opload operation again as commiter provided to
            // clientUpload may not be valid
            LOG_err << "clientUpload (Update mTime): Error(" << e << "), Node(" << toNodeHandle(h)
                    << "). Falling back to full upload transfer";

            mc.syncs.queueClient(
                [uploadWptr = u->weak_from_this(), vo, queueFirst](MegaClient& mc,
                                                                   TransferDbCommitter& committer)
                {
                    if (auto u = uploadWptr.lock())
                    {
                        u->fullUpload(mc, committer, vo, queueFirst);
                    }
                });
        }
    };

    upload->wasStarted = true;
    if (upload->wasJustMtimeChanged)
    {
        auto parent = mc.nodeByHandle(upload->h);
        auto node = parent ? mc.childnodebyname(parent.get(), upload->name.c_str()) : nullptr;
        if (node)
        {
            if (auto immediateResult =
                    upload->updateNodeMtime(&mc, node, upload->mtime, std::move(mtimeCompletion));
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

    if (auto cloneNodeCandidate = findCloneNodeCandidate(mc, *upload, true /*excludeMtime*/);
        cloneNodeCandidate)
    {
        upload->cloneNode(mc, cloneNodeCandidate, ovHandleIfShortcut);
        return;
    }

    upload->fullUpload(mc, committer, vo, queueFirst);
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
                download->completed(nullptr, PUTNODES_SYNC);
                download->wasDistributed = true;
                LOG_debug << "clientDownload: setmtimelocal change only";
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

    LOG_debug << "clientDownload: regular download started";
}

/*************************************\
*  SYNC COMPARISONS - IMPLEMENTATION  *
\*************************************/

namespace
{

// Default throttle window for mtime-only MAC computations.
// Files with frequent mtime changes (like .eml on Windows) benefit from this delay.
constexpr std::chrono::seconds MAC_THROTTLE_WINDOW{30};

/**
 * @brief Quick fingerprint comparison without MAC computation.
 *
 * Compares type, size, CRC, and mtime. If all match except mtime, returns std::nullopt
 * to indicate that MAC computation is needed.
 */
std::optional<FsCloudComparisonResult> quickFingerprintComparison(const CloudNode& cn,
                                                                  const FSNode& fs)
{
    static const std::string logPre{"quickFingerprintComparison: "};

    // Different types -> cannot be equal
    if (cn.type != fs.type)
    {
        return FsCloudComparisonResult{NODE_COMP_DIFFERS_FP, INVALID_META_MAC, INVALID_META_MAC};
    }

    // Folders don't need MAC comparison
    if (cn.type != FILENODE)
    {
        return FsCloudComparisonResult{NODE_COMP_EQUAL, INVALID_META_MAC, INVALID_META_MAC};
    }

    // Check fingerprint (CRC, size, isValid) excluding mtime
    if (!fs.fingerprint.equalExceptMtime(cn.fingerprint))
    {
        if (!fs.fingerprint.isvalid || !cn.fingerprint.isvalid)
        {
            LOG_warn << logPre << "fs isValid (" << fs.fingerprint.isvalid << "), cn isValid ("
                     << cn.fingerprint.isvalid << ")";
            assert(fs.fingerprint.isvalid && cn.fingerprint.isvalid);
        }
        return FsCloudComparisonResult{NODE_COMP_DIFFERS_FP, INVALID_META_MAC, INVALID_META_MAC};
    }

    // Full fingerprint match including mtime -> equal
    // IMPORTANT: We accept fingerprint collision risk here to avoid expensive MAC computation
    if (fs.fingerprint.mtime == cn.fingerprint.mtime)
    {
        return FsCloudComparisonResult{NODE_COMP_EQUAL, INVALID_META_MAC, INVALID_META_MAC};
    }

    // mtime differs but everything else matches -> MAC computation needed
    return std::nullopt;
}

/**
 * @brief Initialize MAC computation state from cloud node.
 *
 * Extracts cipher key, IV, and expected MAC from the node key.
 * Returns false if node is invalid or inaccessible.
 *
 * THREAD SAFETY: This function runs on the sync thread and must hold
 * nodeTreeMutex when accessing node data to prevent races with the client thread.
 */
bool initMacComputationFromNode(MegaClient& mc,
                                const NodeHandle cloudNodeHandle,
                                LocalNode::RareFields::MacComputationInProgress& macComp,
                                const std::string& logPrefix)
{
    // Lock the node tree to safely access node data from the sync thread.
    // The node key and type could change if the client thread is processing updates.
    std::lock_guard<std::recursive_mutex> g(mc.nodeTreeMutex);

    auto node = mc.nodeByHandle(cloudNodeHandle);
    if (!node || node->type != FILENODE || node->nodekey().empty())
    {
        LOG_debug << logPrefix << "Invalid cloud node";
        return false;
    }

    const auto& nodeKey = node->nodekey();

    // Extract cipher key (first 16 bytes, XORed with second 16 bytes)
    memcpy(macComp.transferkey.data(), nodeKey.data(), SymmCipher::KEYLENGTH);
    SymmCipher::xorblock((const byte*)nodeKey.data() + SymmCipher::KEYLENGTH,
                         macComp.transferkey.data());

    // Extract IV and expected MAC from key
    const char* iva = nodeKey.data() + SymmCipher::KEYLENGTH;
    macComp.ctriv = MemAccess::get<int64_t>(iva);
    macComp.expectedMac = MemAccess::get<int64_t>(iva + sizeof(int64_t));

    return true;
}

/**
 * @brief Process one chunk of MAC computation on worker thread.
 *
 * Reads a chunk from the file buffer, computes chunk MACs, and updates state.
 * Called from mAsyncQueue worker thread.
 */
void processChunkOnWorkerThread(
    std::weak_ptr<LocalNode::RareFields::MacComputationInProgress> weakMac,
    m_off_t chunkStart,
    m_off_t chunkEnd,
    std::shared_ptr<byte[]> chunkData,
    const std::string& logPrefix)
{
    auto macComp = weakMac.lock();
    if (!macComp)
    {
        LOG_debug << logPrefix << "Abandoned (LocalNode gone)";
        return;
    }

    // Create cipher and compute chunk MACs
    SymmCipher cipher;
    cipher.setkey(macComp->transferkey.data());

    chunkmac_map chunkMacs;

    // Process using the MEGA chunk boundaries (128KB-1MB chunks)
    m_off_t pos = chunkStart;
    byte* bufPtr = chunkData.get();

    while (pos < chunkEnd)
    {
        m_off_t chunkBoundary = ChunkedHash::chunkceil(pos, macComp->totalSize);
        m_off_t thisChunkEnd = std::min(chunkBoundary, chunkEnd);
        unsigned chunkSize = static_cast<unsigned>(thisChunkEnd - pos);

        // Compute MAC for this chunk
        m_off_t chunkId = ChunkedHash::chunkfloor(pos);
        bool finishesChunk =
            (thisChunkEnd == chunkBoundary) || (thisChunkEnd == macComp->totalSize);

        chunkMacs
            .ctr_encrypt(chunkId, &cipher, bufPtr, chunkSize, pos, macComp->ctriv, finishesChunk);

        bufPtr += chunkSize;
        pos = thisChunkEnd;
    }

    // Check if this was the last chunk
    if (chunkEnd >= macComp->totalSize)
    {
        // Compute final MAC
        {
            std::lock_guard<std::mutex> g(macComp->macsMutex);
            chunkMacs.copyEntriesTo(macComp->partialMacs);
        }

        int64_t localMac = macComp->partialMacs.macsmac(&cipher);
        int64_t remoteMac = macComp->expectedMac;

        auto compRes = (localMac == remoteMac) ? NODE_COMP_DIFFERS_MTIME : NODE_COMP_DIFFERS_MAC;

        LOG_debug << logPrefix << "Complete: " << nodeComparisonResultToStr(compRes)
                  << " [localMac=" << localMac << ", remoteMac=" << remoteMac << "]";

        macComp->setComplete(compRes, localMac, remoteMac);
    }
    else
    {
        // More chunks to process
        macComp->addChunkMacs(std::move(chunkMacs), chunkEnd);
        macComp->chunkInProgress.store(false, std::memory_order_release);

        LOG_verbose << logPrefix << "Chunk done, progress: " << chunkEnd << "/"
                    << macComp->totalSize;
    }
}

/**
 * @brief Initiates incremental async MAC computation for a synced file.
 *
 * Uses a transfer-like pattern:
 * 1. Sync thread reads file in large chunks (10-20MB) with FILE_SHARE_DELETE
 * 2. Worker thread (mAsyncQueue) computes MACs for chunks
 * 3. Progress tracked across sync iterations
 * 4. File opened/closed per chunk to minimize lock duration
 */
/**
 * @brief Helper to release throttle slot when macComp is being cleaned up.
 */
void releaseMacComputationThrottle(MegaClient& mc,
                                   LocalNode::RareFields::MacComputationInProgress* macComp)
{
    if (macComp && macComp->throttleSlotAcquired)
    {
        mc.syncs.mMacComputationThrottle.releaseFile();
        macComp->throttleSlotAcquired = false;
        LOG_verbose << "asyncMacComputation: Released throttle slot for " << macComp->filePath
                    << " [files=" << mc.syncs.mMacComputationThrottle.currentFiles()
                    << ", chunks=" << mc.syncs.mMacComputationThrottle.currentChunks() << "]";
    }
}

FsCloudComparisonResult asyncMacComputation(MegaClient& mc,
                                            const CloudNode& cn,
                                            const FSNode& fs,
                                            const LocalPath& fsNodeFullPath,
                                            LocalNode& syncNode)
{
    static const std::string logPre{"asyncMacComputation: "};
    using MacComp = LocalNode::RareFields::MacComputationInProgress;

    // Check throttling first - skip MAC computation if a recent mtime-only op just completed.
    if (syncNode.shouldThrottleMacComputation(MAC_THROTTLE_WINDOW))
    {
        LOG_verbose << logPre << "Throttled (recent mtime-only op): " << fsNodeFullPath;
        return {NODE_COMP_PENDING, INVALID_META_MAC, INVALID_META_MAC};
    }

    // Check for existing computation
    if (syncNode.hasRare() && syncNode.rareRO().macComputation)
    {
        auto& macComp = syncNode.rare().macComputation;

        // Validate context is still current (handles moves, deletes, content changes)
        if (!macComp->contextMatches(fs.fsid, cn.handle, fs.fingerprint, cn.fingerprint))
        {
            LOG_debug << logPre << "Context invalid, discarding: " << fsNodeFullPath;
            releaseMacComputationThrottle(mc, macComp.get());
            macComp.reset();
            syncNode.trimRareFields();
            // Fall through to initiate new computation
        }
        else if (macComp->hasFailed())
        {
            // Computation failed - return error and clean up
            LOG_debug << logPre << "Failed: " << fsNodeFullPath;
            releaseMacComputationThrottle(mc, macComp.get());
            macComp.reset();
            syncNode.trimRareFields();
            return {NODE_COMP_EREAD, INVALID_META_MAC, INVALID_META_MAC};
        }
        else if (macComp->isReady())
        {
            // Computation complete - extract result and clean up
            auto result = macComp->result;
            auto localMac = macComp->localMac;
            auto remoteMac = macComp->remoteMac;

            LOG_debug << logPre << "Complete: " << nodeComparisonResultToStr(result) << " [path = '"
                      << fsNodeFullPath << "']";

            releaseMacComputationThrottle(mc, macComp.get());
            macComp.reset();
            syncNode.trimRareFields();
            return {result, localMac, remoteMac};
        }
        else if (macComp->isChunkInProgress())
        {
            // Worker still processing - return pending
            LOG_verbose << logPre << "Chunk in progress: " << fsNodeFullPath;
            return {NODE_COMP_PENDING, INVALID_META_MAC, INVALID_META_MAC};
        }
        else
        {
            // Ready for next chunk - fall through to read and queue
        }
    }

    // Initialize or continue computation
    std::shared_ptr<MacComp> macComp;

    if (syncNode.hasRare() && syncNode.rareRO().macComputation)
    {
        macComp = syncNode.rare().macComputation;
    }
    else
    {
        // Check global throttle before starting new file computation
        if (!mc.syncs.mMacComputationThrottle.tryAcquireFile())
        {
            LOG_verbose << logPre << "Throttle full (files), deferring: " << fsNodeFullPath
                        << " [files=" << mc.syncs.mMacComputationThrottle.currentFiles()
                        << ", chunks=" << mc.syncs.mMacComputationThrottle.currentChunks() << "]";
            return {NODE_COMP_PENDING, INVALID_META_MAC, INVALID_META_MAC};
        }

        // Create new computation state
        const m_off_t fileSize = fs.fingerprint.size;
        macComp = std::make_shared<MacComp>(fs.fingerprint,
                                            cn.fingerprint,
                                            cn.handle,
                                            fs.fsid,
                                            fileSize,
                                            fsNodeFullPath);
        macComp->throttleSlotAcquired = true; // Mark that we've acquired a throttle slot
        syncNode.rare().macComputation = macComp;

        // Initialize cipher params from cloud node (must be done on sync thread with node access)
        if (!initMacComputationFromNode(mc, cn.handle, *macComp, logPre))
        {
            releaseMacComputationThrottle(mc, macComp.get());
            macComp.reset();
            syncNode.rare().macComputation.reset();
            syncNode.trimRareFields();
            return {NODE_COMP_EARGS, INVALID_META_MAC, INVALID_META_MAC};
        }

        LOG_debug << logPre << "Initiating: " << fsNodeFullPath << " [size=" << macComp->totalSize
                  << ", files=" << mc.syncs.mMacComputationThrottle.currentFiles()
                  << ", chunks=" << mc.syncs.mMacComputationThrottle.currentChunks()
                  << ", maxMemory=" << mc.syncs.mMacComputationThrottle.limits().maxMemoryUsage()
                  << "]";
    }

    // Read next chunk from file (with FILE_SHARE_DELETE)
    // IMPORTANT: We must align reads to MEGA chunk boundaries because ctr_encrypt
    // requires starting at chunk boundaries. The MEGA chunking scheme has variable
    // chunk sizes (128KB-1MB), so we round down readEnd to ensure complete chunks.
    m_off_t readStart = macComp->currentPosition; // Always at a MEGA chunk boundary
    m_off_t tentativeEnd = std::min(readStart + MacComp::BUFFER_SIZE, macComp->totalSize);

    // Round down to nearest MEGA chunk boundary (unless it's the file end)
    m_off_t readEnd;
    if (tentativeEnd >= macComp->totalSize)
    {
        readEnd = macComp->totalSize; // Read to end of file
    }
    else
    {
        // Find the start of the MEGA chunk containing tentativeEnd
        // This is where our next read should start from
        readEnd = ChunkedHash::chunkfloor(tentativeEnd);
        if (readEnd <= readStart)
        {
            // Buffer too small for even one chunk - extend to next chunk boundary
            readEnd = ChunkedHash::chunkceil(readStart, macComp->totalSize);
        }
    }

    m_off_t readSize = readEnd - readStart;

    if (readSize <= 0)
    {
        // Should not happen - computation should have completed
        LOG_err << logPre << "Invalid read size: " << readSize;
        macComp->setFailed();
        return {NODE_COMP_EREAD, INVALID_META_MAC, INVALID_META_MAC};
    }

    // Open file briefly with FILE_SHARE_DELETE
    auto fa = mc.fsaccess->newfileaccess();
    if (!fa || !fa->fopenForMacRead(fsNodeFullPath, FSLogging::logOnError))
    {
        LOG_debug << logPre << "Cannot open file: " << fsNodeFullPath;
        macComp->setFailed();
        return {NODE_COMP_EREAD, INVALID_META_MAC, INVALID_META_MAC};
    }

    // Verify file size matches expected
    if (fa->size != macComp->totalSize)
    {
        LOG_debug << logPre << "File size changed: expected " << macComp->totalSize << ", got "
                  << fa->size;
        fa->fclose();
        macComp.reset();
        syncNode.rare().macComputation.reset();
        syncNode.trimRareFields();
        // File changed - let sync re-evaluate
        return {NODE_COMP_EREAD, INVALID_META_MAC, INVALID_META_MAC};
    }

    // Read chunk into buffer (use shared_ptr for std::function compatibility)
    auto chunkData =
        std::shared_ptr<byte[]>(new byte[static_cast<size_t>(readSize) + SymmCipher::BLOCKSIZE]);
    memset(chunkData.get() + readSize, 0, SymmCipher::BLOCKSIZE); // Pad for cipher

    bool readOk = fa->frawread(chunkData.get(),
                               static_cast<unsigned>(readSize),
                               readStart,
                               true,
                               FSLogging::logOnError);
    fa->fclose(); // Release file immediately

    if (!readOk)
    {
        LOG_debug << logPre << "Read failed at " << readStart << ": " << fsNodeFullPath;
        macComp->setFailed();
        return {NODE_COMP_EREAD, INVALID_META_MAC, INVALID_META_MAC};
    }

    // Mark chunk in progress and queue to worker thread
    macComp->chunkInProgress.store(true, std::memory_order_release);

    std::weak_ptr<MacComp> weakMac = macComp;
    const std::string workerLogPre = "asyncMacComputation (worker): ";

    mc.mAsyncQueue.push(
        [weakMac, readStart, readEnd, chunkData, workerLogPre](SymmCipher&)
        {
            processChunkOnWorkerThread(weakMac, readStart, readEnd, chunkData, workerLogPre);
        },
        true); // discardable - if client shutting down, don't block

    LOG_verbose << logPre << "Queued chunk [" << readStart << "-" << readEnd
                << "]: " << fsNodeFullPath;

    return {NODE_COMP_PENDING, INVALID_META_MAC, INVALID_META_MAC};
}

/**
 * @brief Blocking MAC computation for unsynced files.
 *
 * Performs full MAC computation in one go (not incremental).
 * Used for SRT_CXF case where there's no LocalNode to store state.
 */
FsCloudComparisonResult blockingMacComputation(MegaClient& mc,
                                               const CloudNode& cn,
                                               const LocalPath& fsNodeFullPath)
{
    static const std::string logPre{"blockingMacComputation: "};
    const auto t0 = std::chrono::steady_clock::now();
    const auto fsNFP = fsNodeFullPath.toPath(false);

    LOG_debug << logPre << "Starting: " << fsNFP;

    auto node = mc.nodeByHandle(cn.handle);
    if (!node || node->type != FILENODE || node->nodekey().empty())
    {
        LOG_debug << logPre << "NODE_COMP_EARGS: " << fsNFP;
        return {NODE_COMP_EARGS, INVALID_META_MAC, INVALID_META_MAC};
    }

    // Open with FILE_SHARE_DELETE
    auto fa = mc.fsaccess->newfileaccess();
    if (!fa || !fa->fopenForMacRead(fsNodeFullPath, FSLogging::logOnError))
    {
        LOG_debug << logPre << "NODE_COMP_EREAD: " << fsNFP;
        return {NODE_COMP_EREAD, INVALID_META_MAC, INVALID_META_MAC};
    }

    auto [localMac, remoteMac] =
        genLocalAndRemoteMetaMac(fa.get(), node->nodekey(), node->type, fsNFP);

    fa->fclose();

    auto compRes = (localMac == remoteMac) ? NODE_COMP_DIFFERS_MTIME : NODE_COMP_DIFFERS_MAC;

    const auto t1 = std::chrono::steady_clock::now();
    const auto msMac = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    LOG_debug << logPre << "Complete in " << msMac << " ms: " << nodeComparisonResultToStr(compRes)
              << " [path = '" << fsNFP << "']";

    return {compRes, localMac, remoteMac};
}

} // anonymous namespace

FsCloudComparisonResult syncEqualFsCloudExcludingMtimeAsync(MegaClient& mc,
                                                            const CloudNode& cn,
                                                            const FSNode& fs,
                                                            const LocalPath& fsNodeFullPath,
                                                            LocalNode& syncNode)
{
    // Quick fingerprint comparison (no MAC needed if conclusive)
    if (auto quickResult = quickFingerprintComparison(cn, fs))
    {
        return *quickResult;
    }

    // mtime differs - need async MAC computation
    return asyncMacComputation(mc, cn, fs, fsNodeFullPath, syncNode);
}

FsCloudComparisonResult syncEqualFsCloudExcludingMtimeSync(MegaClient& mc,
                                                           const CloudNode& cn,
                                                           const FSNode& fs,
                                                           const LocalPath& fsNodeFullPath)
{
    // Quick fingerprint comparison (no MAC needed if conclusive)
    if (auto quickResult = quickFingerprintComparison(cn, fs))
    {
        return *quickResult;
    }

    // mtime differs - need blocking MAC computation
    return blockingMacComputation(mc, cn, fsNodeFullPath);
}
} // namespace mega

#endif // ENABLE_SYNC