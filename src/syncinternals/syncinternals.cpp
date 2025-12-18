/**
 * @file syncinternals.cpp
 * @brief Class for internal operations of the sync engine.
 */

#include "mega/base64.h"
#ifdef ENABLE_SYNC

#include "mega/crypto/cryptopp.h"
#include "mega/megaclient.h"
#include "mega/sync.h"
#include "mega/syncinternals/syncinternals.h"
#include "mega/syncinternals/syncinternals_logging.h"
#include "mega/utils.h"

#include <array>
#include <cstdint>
#include <limits>
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
     * Checks if the node matches the local file in terms of Fingerprint, and meta MAC if
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

        if (!mUpload.mMetaMac.has_value() || mUpload.mMetaMac.value() == INVALID_META_MAC)
        {
            LOG_err << logPre << "mMetaMac "
                    << (mUpload.mMetaMac.has_value() ? "has invalid meta MAC" : "is not set")
                    << " for " << nodePath << " !! Skip cloning node";
            assert(false && (("mMetaMac is not set for " + std::string(nodePath)).c_str()));
            return false;
        }

        const auto compRes =
            CompareNodeWithProvidedMacAndFpExcludingMtime(&node, mUpload, *mUpload.mMetaMac);
        const auto compResStr = nodeComparisonResultToStr(compRes);

        if (compRes != NODE_COMP_EQUAL && compRes != NODE_COMP_DIFFERS_MTIME)
        {
            LOG_err << logPre << compResStr << " -> return false [path = " << nodePath << "]";
            return false;
        }

        if (node.hasZeroKey())
        {
            LOG_warn << "Clone node key is a zero key!! Avoid cloning node [path = '" << nodePath
                     << "', sourceLocalname = '" << mUpload.sourceLocalname << "']";
            mClient.sendevent(99486, "Node has a zerokey");
            mFoundCandidateHasZeroKey = true;
        }

        LOG_debug << logPre << compResStr << " -> return true [path = " << nodePath << "]";
        return true;
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
    if (!upload.mMetaMac.has_value() || upload.mMetaMac.value() == INVALID_META_MAC)
    {
        LOG_warn << "findCloneNodeCandidate: mMetaMac "
                 << (upload.mMetaMac.has_value() ? "has invalid meta MAC" : "is not set") << " for "
                 << upload.getLocalname() << " !! Skip cloning node";
        assert(false && ("mMetaMac is not set for " + upload.getLocalname().toPath(false)).c_str());
        return nullptr;
    }

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

CloneMacStatus initCloneCandidateMacComputation(MegaClient& mc, SyncUpload_inClient& upload);

void clientUpload(MegaClient& mc,
                  TransferDbCommitter& committer,
                  std::shared_ptr<SyncUpload_inClient> upload,
                  const VersioningOption vo,
                  const bool queueFirst,
                  const NodeHandle ovHandleIfShortcut)
{
    assert(!upload->wasStarted);

    const auto startCloneOrFullUpload =
        [&mc, vo, queueFirst, ovHandleIfShortcut](std::shared_ptr<SyncUpload_inClient> u)
    {
        mc.syncs.queueClient(
            [uploadWptr = u->weak_from_this(), vo, queueFirst, ovHandleIfShortcut](
                MegaClient& mc,
                TransferDbCommitter& committer)
            {
                if (auto u = uploadWptr.lock())
                {
                    const auto macState = initCloneCandidateMacComputation(mc, *u);
                    processCloneMacResult(mc,
                                          committer,
                                          u,
                                          vo,
                                          queueFirst,
                                          ovHandleIfShortcut,
                                          macState);
                }
            });
    };

    auto mtimeCompletion = [&mc,
                            ovHandleIfShortcut,
                            startCloneOrFullUpload,
                            uploadWptr = upload->weak_from_this()](NodeHandle h, Error e)
    {
        if (auto u = uploadWptr.lock())
        {
            // Set `upSyncFailed` to `False` regardless of settAttr result.
            // - If setAttr succeeded => upSyncFailed must be `False`
            // - If setAttr failed we want to execute fallback mechanism (Full upload or Clone
            // Node), so we need to reset flag anyway
            u->upsyncFailed = false;

            if (auto setMtimeSucceeded = e == API_OK; setMtimeSucceeded)
            {
                // Let the engine know the setAttr has been completed successfully.
                u->upsyncResultHandle = h;
                u->wasUpsyncCompleted.store(true);
                return;
            }

            LOG_err << "clientUpload (Update mTime): Error(" << e << "), Node(" << toNodeHandle(h)
                    << "). Falling back to Cloning node";

            u->upsyncStarted = false;
            u->attributeOnlyUpdate = SyncTransfer_inClient::AttributeOnlyUpdate::None;
            if (!u->mMetaMac.has_value())
            {
                LOG_err << "clientUpload (Update mTime): mMetaMac is not set for "
                        << u->getLocalname() << " !!!! Skip cloning node";
                assert(false &&
                       ("mMetaMac is not set for " + u->getLocalname().toPath(false)).c_str());
            }
            else if (auto cloneNodeCandidate =
                         findCloneNodeCandidate(mc, *u, true /*excludeMtime*/);
                     cloneNodeCandidate)
            {
                u->cloneNode(mc, cloneNodeCandidate, ovHandleIfShortcut);
                return;
            }

            LOG_err << "clientUpload (Update mTime): No clone candidate found for Node("
                    << toNodeHandle(h) << "). Falling back to full upload transfer / cloning";
            startCloneOrFullUpload(std::move(u));
        }
    };

    auto crcCompletion =
        [startCloneOrFullUpload, uploadWptr = upload->weak_from_this()](NodeHandle h, Error e)
    {
        if (auto u = uploadWptr.lock())
        {
            u->upsyncFailed = false;

            if (e == API_OK)
            {
                u->upsyncResultHandle = h;
                u->wasUpsyncCompleted.store(true);
                return;
            }

            LOG_err << "clientUpload (Update CRC): Error(" << e << "), Node(" << toNodeHandle(h)
                    << "). Falling back to full upload transfer / cloning";

            u->upsyncStarted = false;
            u->attributeOnlyUpdate = SyncTransfer_inClient::AttributeOnlyUpdate::None;

            startCloneOrFullUpload(std::move(u));
        }
    };

    upload->wasStarted = true;
    const auto attributeOnlyUpdate = upload->attributeOnlyUpdate.load();
    if (attributeOnlyUpdate == SyncTransfer_inClient::AttributeOnlyUpdate::MtimeOnly)
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
            upload->attributeOnlyUpdate = SyncTransfer_inClient::AttributeOnlyUpdate::None;
        }
        else
        {
            LOG_warn << "clientUpload: "
                     << (!parent ? "Parent Node not found" : "Cloud Node not found")
                     << ". Falling back to full upload transfer / Cloning node";
            assert(false);
            upload->attributeOnlyUpdate = SyncTransfer_inClient::AttributeOnlyUpdate::None;
        }
    }
    else if (attributeOnlyUpdate == SyncTransfer_inClient::AttributeOnlyUpdate::CrcOnly)
    {
        auto parent = mc.nodeByHandle(upload->h);
        auto node = parent ? mc.childnodebyname(parent.get(), upload->name.c_str()) : nullptr;

        if (node)
        {
            const auto& cloudFp = node->fingerprint();
            const auto& localFp = upload->fingerprint();

            if (cloudFp.isvalid && localFp.isvalid && cloudFp.size == localFp.size &&
                cloudFp.mtime == localFp.mtime && !areCrcEqual(cloudFp.crc, localFp.crc))
            {
                std::array<std::int32_t, LEGACY_CRC_LANES> legacyCrc{};
                if (computeLegacyBuggySparseCrc(mc,
                                                upload->getLocalname(),
                                                localFp.size,
                                                legacyCrc) &&
                    areCrcEqual(cloudFp.crc, legacyCrc))
                {
                    if (auto immediateResult =
                            mc.updateNodeFingerprint(node, localFp, std::move(crcCompletion));
                        immediateResult == API_OK)
                    {
                        upload->wasFileTransferCompleted = true;
                        upload->upsyncStarted = true;
                        return;
                    }

                    LOG_warn << "clientUpload: UpdateCRC immediate error. Falling back to full "
                                "upload transfer / Cloning node";
                    upload->attributeOnlyUpdate = SyncTransfer_inClient::AttributeOnlyUpdate::None;
                }
                else
                {
                    LOG_warn << "clientUpload: computeLegacyBuggySparseCrc failed. Falling back to "
                                "full upload transfer / Cloning node";
                }
            }
        }
        else
        {
            LOG_warn << "clientUpload: "
                     << (!parent ? "Parent Node not found" : "Cloud Node not found")
                     << ". Falling back to full upload transfer / Cloning node";
            upload->attributeOnlyUpdate = SyncTransfer_inClient::AttributeOnlyUpdate::None;
        }

        // If we didn't early-return, proceed with the normal clone/full-upload path.
        upload->attributeOnlyUpdate = SyncTransfer_inClient::AttributeOnlyUpdate::None;
    }

    assert(!upload->macComputation);

    const auto macState = initCloneCandidateMacComputation(mc, *upload);
    processCloneMacResult(mc, committer, upload, vo, queueFirst, ovHandleIfShortcut, macState);
}

/******************\
*  SYNC DOWNLOADS  *
\******************/
void clientDownload(MegaClient& mc,
                    TransferDbCommitter& committer,
                    std::shared_ptr<SyncDownload_inClient> download,
                    const bool queueFirst)
{
    if (download->attributeOnlyUpdate.load() ==
        SyncTransfer_inClient::AttributeOnlyUpdate::MtimeOnly)
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

/**
 * @brief Quick fingerprint comparison without MAC computation.
 *
 * Compares type, size, CRC, and mtime. Returns a conclusive result if possible,
 * or std::nullopt if only mtime differs (indicating MAC computation is needed).
 */
std::optional<FsCloudComparisonResult> quickFingerprintComparison(const CloudNode& cn,
                                                                  const FSNode& fs)
{
    static const std::string logPre{"quickFingerprintComparison: "};
    const auto mismatchKind = [&]() -> FingerprintMismatch
    {
        if (cn.type != fs.type)
        {
            return FingerprintMismatch::Other;
        }

        if (cn.type != FILENODE)
        {
            return FingerprintMismatch::None;
        }

        const auto& a = fs.fingerprint;
        const auto& b = cn.fingerprint;

        if (a.size != b.size || !a.isvalid || !b.isvalid)
        {
            return FingerprintMismatch::Other;
        }

        const bool crcEqual = areCrcEqual(a.crc, b.crc);
        const bool sameMtime = a.mtime == b.mtime;

        if (crcEqual)
        {
            return sameMtime ? FingerprintMismatch::None : FingerprintMismatch::MtimeOnly;
        }

        return sameMtime ? FingerprintMismatch::CrcOnly : FingerprintMismatch::Other;
    };

    // Different types -> cannot be equal
    if (cn.type != fs.type)
    {
        return FsCloudComparisonResult{NODE_COMP_DIFFERS_FP,
                                       INVALID_META_MAC,
                                       INVALID_META_MAC,
                                       FingerprintMismatch::Other};
    }

    // Folders don't need MAC comparison
    if (cn.type != FILENODE)
    {
        return FsCloudComparisonResult{NODE_COMP_EQUAL,
                                       INVALID_META_MAC,
                                       INVALID_META_MAC,
                                       FingerprintMismatch::None};
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
        return FsCloudComparisonResult{NODE_COMP_DIFFERS_FP,
                                       INVALID_META_MAC,
                                       INVALID_META_MAC,
                                       mismatchKind()};
    }

    // Full fingerprint match including mtime -> equal
    // IMPORTANT: We accept fingerprint collision risk here to avoid expensive MAC computation
    if (fs.fingerprint.mtime == cn.fingerprint.mtime)
    {
        return FsCloudComparisonResult{NODE_COMP_EQUAL,
                                       INVALID_META_MAC,
                                       INVALID_META_MAC,
                                       FingerprintMismatch::None};
    }

    // mtime differs but everything else matches -> MAC computation needed
    return std::nullopt;
}

/**
 * @brief Process one chunk of MAC computation on worker thread.
 *
 * Shared by CSF async MAC and clone candidate MAC computation.
 * Called from mAsyncQueue worker thread.
 */
void processChunkOnWorkerThread(std::weak_ptr<MacComputationState> weakMac,
                                m_off_t chunkStart,
                                m_off_t chunkEnd,
                                std::shared_ptr<byte[]> chunkData,
                                const std::string& logPrefix)
{
    auto macComp = weakMac.lock();
    if (!macComp)
    {
        LOG_debug << logPrefix << "Abandoned (owner gone)";
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
        // Compute final local MAC
        {
            std::lock_guard<std::mutex> g(macComp->macsMutex);
            chunkMacs.copyEntriesTo(macComp->partialMacs);
        }

        int64_t localMac = macComp->partialMacs.macsmac(&cipher);

        LOG_debug << logPrefix << "Local MAC computed: " << localMac;

        macComp->setComplete(localMac);
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
 * @brief Result of advanceMacComputation.
 */
enum class MacAdvanceResult
{
    Pending, // Chunk queued or in progress
    Ready, // Local MAC is computed (check state->localMac)
    Failed // Error occurred
};

/**
 * @brief Advance local file MAC computation by one chunk.
 *
 * This is the shared core for both CSF and clone candidate MAC computation.
 * It handles reading the next chunk and queueing it to the worker thread.
 *
 * @param mc MegaClient for file access and async queue.
 * @param state The MAC computation state (must have cipher params initialized).
 * @param logPrefix Prefix for log messages.
 * @return MacAdvanceResult indicating current state.
 */
MacAdvanceResult advanceMacComputation(MegaClient& mc,
                                       std::shared_ptr<MacComputationState>& state,
                                       const std::string& logPrefix)
{
    if (!state)
    {
        return MacAdvanceResult::Failed;
    }

    // Check current state
    if (state->hasFailed())
    {
        return MacAdvanceResult::Failed;
    }

    if (state->isReady())
    {
        return MacAdvanceResult::Ready;
    }

    if (state->isChunkInProgress())
    {
        return MacAdvanceResult::Pending;
    }

    // Ready for next chunk - read and queue
    m_off_t readStart = state->currentPosition;
    m_off_t tentativeEnd = std::min(readStart + MacComputationState::BUFFER_SIZE, state->totalSize);

    // Round down to nearest MEGA chunk boundary (unless it's the file end)
    m_off_t readEnd;
    if (tentativeEnd >= state->totalSize)
    {
        readEnd = state->totalSize;
    }
    else
    {
        readEnd = ChunkedHash::chunkfloor(tentativeEnd);
        if (readEnd <= readStart)
        {
            readEnd = ChunkedHash::chunkceil(readStart, state->totalSize);
        }
    }

    m_off_t readSize = readEnd - readStart;

    if (readSize <= 0)
    {
        LOG_err << logPrefix << "Invalid read size: " << readSize;
        state->setFailed();
        return MacAdvanceResult::Failed;
    }

    // Open file briefly with FILE_SHARE_DELETE
    auto fa = mc.fsaccess->newfileaccess();
    if (!fa || !fa->fopenForMacRead(state->filePath, FSLogging::logOnError))
    {
        LOG_debug << logPrefix << "Cannot open file: " << state->filePath;
        state->setFailed();
        return MacAdvanceResult::Failed;
    }

    // Verify file size matches expected
    if (fa->size != state->totalSize)
    {
        LOG_debug << logPrefix << "File size changed: expected " << state->totalSize << ", got "
                  << fa->size;
        fa->fclose();
        state->setFailed();
        return MacAdvanceResult::Failed;
    }

    // Read chunk into buffer
    auto chunkData =
        std::shared_ptr<byte[]>(new byte[static_cast<size_t>(readSize) + SymmCipher::BLOCKSIZE]);
    memset(chunkData.get() + readSize, 0, SymmCipher::BLOCKSIZE);

    bool readOk = fa->frawread(chunkData.get(),
                               static_cast<unsigned>(readSize),
                               readStart,
                               true,
                               FSLogging::logOnError);
    fa->fclose();

    if (!readOk)
    {
        LOG_debug << logPrefix << "Read failed at " << readStart << ": " << state->filePath;
        state->setFailed();
        return MacAdvanceResult::Failed;
    }

    // Mark chunk in progress and queue to worker thread
    state->chunkInProgress.store(true, std::memory_order_release);

    std::weak_ptr<MacComputationState> weakMac = state;
    const std::string workerLogPre = logPrefix + "(worker): ";

    mc.mAsyncQueue.push(
        [weakMac, readStart, readEnd, chunkData, workerLogPre](SymmCipher&)
        {
            processChunkOnWorkerThread(weakMac, readStart, readEnd, chunkData, workerLogPre);
        },
        true);

    LOG_verbose << logPrefix << "Queued chunk [" << readStart << "-" << readEnd
                << "]: " << state->filePath;

    return MacAdvanceResult::Pending;
}

MacComputationState::~MacComputationState()
{
    if (throttleSlotAcquired)
    {
        mThrottle.releaseFile();
        throttleSlotAcquired = false;
        LOG_verbose << "MacComputationState: Released throttle slot for " << filePath;
    }
}

namespace
{

// Default throttle window for mtime-only MAC computations.
// Files with frequent mtime changes (like .eml on Windows) benefit from this delay.
constexpr std::chrono::seconds MAC_THROTTLE_WINDOW{60};

/**
 * @brief Initialize MAC computation cipher params from cloud node.
 *
 * Extracts cipher key and IV from the node key.
 * Returns the expected MAC (for later comparison) or INVALID_META_MAC on failure.
 *
 * THREAD SAFETY: This function runs on the sync thread and must hold
 * nodeTreeMutex when accessing node data to prevent races with the client thread.
 */
int64_t initMacComputationFromNode(MegaClient& mc,
                                   const NodeHandle cloudNodeHandle,
                                   MacComputationState& macComp,
                                   const std::string& logPrefix)
{
    // Lock the node tree to safely access node data from the sync thread.
    // The node key and type could change if the client thread is processing updates.
    std::lock_guard<std::recursive_mutex> g(mc.nodeTreeMutex);

    auto node = mc.nodeByHandle(cloudNodeHandle);
    if (!node || node->type != FILENODE || node->nodekey().empty())
    {
        LOG_debug << logPrefix << "Invalid cloud node";
        return INVALID_META_MAC;
    }

    const auto& nodeKey = node->nodekey();

    // Extract cipher key (first 16 bytes, XORed with second 16 bytes)
    memcpy(macComp.transferkey.data(), nodeKey.data(), SymmCipher::KEYLENGTH);
    SymmCipher::xorblock((const byte*)nodeKey.data() + SymmCipher::KEYLENGTH,
                         macComp.transferkey.data());

    // Extract IV and expected MAC from key
    const char* iva = nodeKey.data() + SymmCipher::KEYLENGTH;
    macComp.ctriv = MemAccess::get<int64_t>(iva);
    int64_t expectedMac = MemAccess::get<int64_t>(iva + sizeof(int64_t));

    return expectedMac;
}

/**
 * @brief Async MAC computation for CSF case (synced files with mtime difference).
 *
 * Uses advanceMacComputation() as the shared core, with context validation
 * specific to CSF case (file/cloud node changes detection).
 */
FsCloudComparisonResult asyncMacComputation(MegaClient& mc,
                                            const CloudNode& cn,
                                            const FSNode& fs,
                                            const LocalPath& fsNodeFullPath,
                                            LocalNode& syncNode)
{
    static const std::string logPre{"asyncMacComputation: "};

    // Check throttling first - skip if a recent mtime-only op just completed
    if (syncNode.shouldThrottleMacComputation(MAC_THROTTLE_WINDOW))
    {
        LOG_verbose << logPre << "Throttled (recent mtime-only op): " << fsNodeFullPath;
        return {NODE_COMP_PENDING, INVALID_META_MAC, INVALID_META_MAC, FingerprintMismatch::Other};
    }

    // Check for existing computation
    if (syncNode.hasRare() && syncNode.rareRO().macComputation)
    {
        auto& macComp = syncNode.rare().macComputation;

        // Validate context is still current (handles moves, deletes, content changes)
        if (!macComp->contextMatches(fs.fsid, cn.handle, fs.fingerprint, cn.fingerprint))
        {
            LOG_debug << logPre << "Context invalid, discarding: " << fsNodeFullPath;
            syncNode.resetMacComputationIfAny();
            // Fall through to initiate new computation
        }
        else
        {
            // Context valid - advance or check result
            auto result = advanceMacComputation(mc, macComp, logPre);

            if (result == MacAdvanceResult::Ready)
            {
                // Local MAC ready - compare with expected (remote) MAC
                int64_t localMac = macComp->localMac;
                int64_t remoteMac =
                    macComp->context ? macComp->context->expectedMac : INVALID_META_MAC;

                auto compRes =
                    (localMac == remoteMac) ? NODE_COMP_DIFFERS_MTIME : NODE_COMP_DIFFERS_MAC;

                LOG_debug << logPre << "Complete: " << nodeComparisonResultToStr(compRes)
                          << " [localMac=" << localMac << ", remoteMac=" << remoteMac << ", path='"
                          << fsNodeFullPath << "']";

                syncNode.resetMacComputationIfAny();
                return {compRes,
                        localMac,
                        remoteMac,
                        compRes == NODE_COMP_DIFFERS_MTIME ? FingerprintMismatch::MtimeOnly :
                                                             FingerprintMismatch::Other};
            }
            else if (result == MacAdvanceResult::Failed)
            {
                LOG_debug << logPre << "Failed: " << fsNodeFullPath;
                syncNode.resetMacComputationIfAny();
                return {NODE_COMP_EREAD,
                        INVALID_META_MAC,
                        INVALID_META_MAC,
                        FingerprintMismatch::Other};
            }
            else
            {
                // Pending - return and wait
                return {NODE_COMP_PENDING,
                        INVALID_META_MAC,
                        INVALID_META_MAC,
                        FingerprintMismatch::Other};
            }
        }
    }

    // No existing computation - start new one

    // Check global throttle before starting new file computation
    if (!mc.syncs.macComputationThrottle().tryAcquireFile())
    {
        LOG_verbose << logPre << "Throttle full, deferring: " << fsNodeFullPath
                    << " [files=" << mc.syncs.macComputationThrottle().currentFiles() << "]";
        return {NODE_COMP_PENDING, INVALID_META_MAC, INVALID_META_MAC, FingerprintMismatch::Other};
    }

    // Create new computation state
    const m_off_t fileSize = fs.fingerprint.size;
    auto macComp = std::make_shared<MacComputationState>(fileSize,
                                                         fsNodeFullPath,
                                                         mc.syncs.macComputationThrottle());
    macComp->throttleSlotAcquired = true;

    // Store CSF context for validation
    macComp->context = MacComputationContext{};
    macComp->context->localFp = fs.fingerprint;
    macComp->context->cloudFp = cn.fingerprint;
    macComp->context->cloudHandle = cn.handle;
    macComp->context->fsid = fs.fsid;

    // Initialize cipher params and get expected MAC
    int64_t expectedMac = initMacComputationFromNode(mc, cn.handle, *macComp, logPre);
    if (expectedMac == INVALID_META_MAC)
    {
        return {NODE_COMP_EARGS, INVALID_META_MAC, INVALID_META_MAC, FingerprintMismatch::Other};
    }
    macComp->context->expectedMac = expectedMac;

    // Store in syncNode
    syncNode.rare().macComputation = macComp;

    LOG_debug << logPre << "Initiating: " << fsNodeFullPath << " [size=" << fileSize
              << ", files=" << mc.syncs.macComputationThrottle().currentFiles() << "]";

    // Advance computation (will queue first chunk)
    auto result = advanceMacComputation(mc, macComp, logPre);

    if (result == MacAdvanceResult::Failed)
    {
        syncNode.resetMacComputationIfAny();
        return {NODE_COMP_EREAD, INVALID_META_MAC, INVALID_META_MAC, FingerprintMismatch::Other};
    }

    return {NODE_COMP_PENDING, INVALID_META_MAC, INVALID_META_MAC, FingerprintMismatch::Other};
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

    // If there's a pending upload marked as mtime-only, skip re-computing MAC and trust the
    // previously validated content unless state changed.
    if (!syncNode.hasRare() || !syncNode.rareRO().macComputation)
    {
        if (auto pendingUpload =
                std::dynamic_pointer_cast<SyncUpload_inClient>(syncNode.transferSP);
            pendingUpload &&
            pendingUpload->attributeOnlyUpdate.load() ==
                SyncTransfer_inClient::AttributeOnlyUpdate::MtimeOnly &&
            pendingUpload->fingerprint().equalExceptMtime(fs.fingerprint))
        {
            // Optional sanity: warn if MetaMAC was not captured (unlikely).
            if (!pendingUpload->mMetaMac.has_value() ||
                pendingUpload->mMetaMac.value() == INVALID_META_MAC)
            {
                LOG_verbose << "syncEqualFsCloudExcludingMtimeAsync: mtime-only pending upload "
                            << "without valid mMetaMac (proceeding without recompute): "
                            << fsNodeFullPath;
            }

            LOG_debug << "syncEqualFsCloudExcludingMtimeAsync: reuse mtime-only pending upload, "
                      << "skipping MAC recomputation: " << fsNodeFullPath;
            return {NODE_COMP_DIFFERS_MTIME,
                    INVALID_META_MAC,
                    INVALID_META_MAC,
                    FingerprintMismatch::MtimeOnly};
        }
    }

    // mtime differs - need async MAC computation
    return asyncMacComputation(mc, cn, fs, fsNodeFullPath, syncNode);
}

/***********************************\
*  CLONE CANDIDATE MAC COMPUTATION  *
\***********************************/

CloneMacStatus initCloneCandidateMacComputation(MegaClient& mc, SyncUpload_inClient& upload)
{
    static const std::string logPre{"initCloneCandidateMac: "};

    // Already have MAC - nothing to init
    if (upload.mMetaMac.has_value())
    {
        return CloneMacStatus::Ready;
    }

    // Already have computation in progress
    if (upload.macComputation)
    {
        return CloneMacStatus::Pending;
    }

    // Get first potential candidate to extract cipher params
    auto candidates = mc.mNodeManager.getNodesByFingerprint(upload, true /*excludeMtime*/);
    if (candidates.empty())
    {
        LOG_debug << logPre << "No candidates found: " << upload.getLocalname();
        return CloneMacStatus::NoCandidates;
    }

    // Find first valid candidate for cipher params
    std::shared_ptr<Node> candidateNode;
    for (const auto& node: candidates)
    {
        if (node && node->type == FILENODE && !node->nodekey().empty())
        {
            candidateNode = node;
            break;
        }
    }

    if (!candidateNode)
    {
        LOG_debug << logPre << "No valid candidate node: " << upload.getLocalname();
        return CloneMacStatus::NoCandidates;
    }

    // Create computation state (no context needed - upload lifetime handles validity)
    const m_off_t fileSize = upload.size;
    upload.macComputation = std::make_shared<MacComputationState>(fileSize,
                                                                  upload.getLocalname(),
                                                                  mc.syncs.mMacComputationThrottle);

    // Initialize cipher params from candidate node
    const auto& nodeKey = candidateNode->nodekey();
    auto& macComp = *upload.macComputation;

    macComp.cloneCandidateHandle = candidateNode->nodeHandle();
    macComp.cloneCandidateNodeKey = nodeKey;

    memcpy(macComp.transferkey.data(), nodeKey.data(), SymmCipher::KEYLENGTH);
    SymmCipher::xorblock((const byte*)nodeKey.data() + SymmCipher::KEYLENGTH,
                         macComp.transferkey.data());

    const char* iva = nodeKey.data() + SymmCipher::KEYLENGTH;
    macComp.ctriv = MemAccess::get<int64_t>(iva);

    // Try to acquire throttle now; if not, leave computation set up and return Pending
    if (mc.syncs.macComputationThrottle().tryAcquireFile())
    {
        macComp.throttleSlotAcquired = true;

        LOG_debug << logPre << "Initiating: " << upload.getLocalname() << " [size=" << fileSize
                  << ", files=" << mc.syncs.macComputationThrottle().currentFiles() << "]";

        // Start first chunk
        auto result = advanceMacComputation(mc, upload.macComputation, logPre);
        if (result == MacAdvanceResult::Failed)
        {
            LOG_debug << logPre << "Failed to start computation of MAC for "
                      << upload.getLocalname();
            upload.macComputation.reset();
            return CloneMacStatus::Failed;
        }
    }
    else
    {
        LOG_verbose << logPre << "Throttle full, deferred start: " << upload.getLocalname()
                    << " [files=" << mc.syncs.macComputationThrottle().currentFiles() << "]";
    }

    return CloneMacStatus::Pending; // Computation created; may be waiting for throttle or running
}

CloneMacStatus checkPendingCloneMac(MegaClient& mc, SyncUpload_inClient& upload)
{
    static const std::string logPre{"checkPendingCloneMac: "};

    if (!upload.macComputation)
    {
        return CloneMacStatus::NoCandidates;
    }

    auto& macComp = upload.macComputation;

    if (upload.mMetaMac.has_value())
    {
        LOG_warn << logPre
                 << "Already have MAC with macComputation still living: " << upload.getLocalname()
                 << " [throttleSlotAcquired=" << macComp->throttleSlotAcquired << "]";
        upload.macComputation.reset();
        return CloneMacStatus::Ready;
    }

    // If we never acquired throttle, try now before advancing
    if (!macComp->throttleSlotAcquired)
    {
        if (!mc.syncs.macComputationThrottle().tryAcquireFile())
        {
            LOG_verbose << logPre << "Throttle still full, pending: " << upload.getLocalname()
                        << " [files=" << mc.syncs.macComputationThrottle().currentFiles() << "]";
            return CloneMacStatus::Pending;
        }

        macComp->throttleSlotAcquired = true;
        LOG_debug << logPre << "Acquired throttle, resuming: " << upload.getLocalname()
                  << " [files=" << mc.syncs.macComputationThrottle().currentFiles() << "]";
    }

    // Validate that the original candidate still exists and has the same key
    if (!macComp->cloneCandidateHandle.isUndef())
    {
        std::lock_guard<std::recursive_mutex> g(mc.nodeTreeMutex);
        auto node = mc.nodeByHandle(macComp->cloneCandidateHandle);
        const bool valid = node && node->type == FILENODE && !node->nodekey().empty() &&
                           node->nodekey() == macComp->cloneCandidateNodeKey;

        if (!valid)
        {
            LOG_debug << logPre << "Candidate removed/changed, aborting computation of MAC for "
                      << upload.getLocalname();
            upload.macComputation.reset();
            return CloneMacStatus::Failed;
        }
    }

    // Advance computation
    auto result = advanceMacComputation(mc, macComp, logPre);

    if (result == MacAdvanceResult::Ready)
    {
        // Local MAC computed - store it for findCloneNodeCandidate
        upload.mMetaMac.emplace(macComp->localMac);
        LOG_debug << logPre << "Complete: MAC=" << macComp->localMac << " [path='"
                  << upload.getLocalname() << "']";

        upload.macComputation.reset();
        return CloneMacStatus::Ready;
    }
    else if (result == MacAdvanceResult::Failed)
    {
        LOG_debug << logPre << "Failed: " << upload.getLocalname();
        upload.macComputation.reset();
        return CloneMacStatus::Failed;
    }

    // Pending
    return CloneMacStatus::Pending;
}

void processCloneMacResult(MegaClient& mc,
                           TransferDbCommitter& committer,
                           std::shared_ptr<SyncUpload_inClient> upload,
                           const VersioningOption vo,
                           const bool queueFirst,
                           const NodeHandle ovHandleIfShortcut,
                           const CloneMacStatus macStatus)
{
    if (!upload)
    {
        return;
    }

    switch (macStatus)
    {
        case CloneMacStatus::Pending:
            // MAC computation in progress or throttled.
            // Leave wasStarted = true (already set above) and return.
            // The sync thread will check progress via checkPendingCloneMac in resolve_upsync.
            LOG_verbose << "processCloneMacResult: MAC computation pending: "
                        << upload->getLocalname();
            return;

        case CloneMacStatus::Ready:
            // MAC ready - proceed with clone candidate search (non-blocking helper).
            if (auto cloneNodeCandidate =
                    findCloneNodeCandidate(mc, *upload, true /*excludeMtime*/))
            {
                LOG_debug << "processCloneMacResult: Proceeding with clone of candidate node for "
                          << upload->getLocalname();
                upload->cloneNode(mc, cloneNodeCandidate, ovHandleIfShortcut);
                return;
            }
            LOG_warn
                << "processCloneMacResult: MAC was computed, but no clone candidate matched for "
                << upload->getLocalname() << " !! Falling back to full upload";
            break;

        case CloneMacStatus::Failed:
            LOG_warn << "processCloneMacResult: MAC computation failed for "
                     << upload->getLocalname() << " !! Falling back to full upload";
            break;

        case CloneMacStatus::NoCandidates:
            LOG_warn << "processCloneMacResult: No clone candidates found for "
                     << upload->getLocalname() << ". Falling back to full upload";
            break;
    }

    upload->fullUpload(mc, committer, vo, queueFirst);
}

} // namespace mega

#endif // ENABLE_SYNC
