/**
 * @file syncinternals.cpp
 * @brief Class for internal operations of the sync engine.
 */

#ifdef ENABLE_SYNC

#include "mega/syncinternals.h"

#include "mega/megaclient.h"
#include "mega/syncinternals_logging.h"

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
    if (source.nodetype == FILENODE && target.fingerprint != source.fingerprint)
        return NodeMatchByFSIDResult::DifferentFingerprint;

    return NodeMatchByFSIDResult::Matched;
}

bool FindLocalNodeByFSIDPredicate::operator()(LocalNode& localNode)
{
    if (mEarlyExit)
        return false;

    const NodeMatchByFSIDAttributes sourceNodeAttributes{localNode.type,
                                                         localNode.sync->fsfp(),
                                                         localNode.sync->cloudRootOwningUser,
                                                         getFingerprint(localNode)};
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

bool FindCloneNodeCandidatePredicate::operator()(const Node& node)
{
    const std::string cloudExtension = mExtractExtensionFromNode(node);
    if (mClient.treatAsIfFileDataEqual(node, mLocalExtension, *mUpload, cloudExtension))
    {
        // Found a candidate that matches content
        if (node.hasZeroKey())
        {
            LOG_warn << "Clone node key is a zero key!! Avoid cloning node [path = '"
                     << node.displaypath() << "', sourceLocalname = '" << mUpload->sourceLocalname
                     << "']";
            mClient.sendevent(99486, "Node has a zerokey");
            mFoundCandidateHasZeroKey = true;
        }
        return true; // Done searching (zero key or valid node)
    }
    return false; // keep searching
}

sharedNode_vector::const_iterator
    findCloneNodeCandidate_if(const sharedNode_vector& candidates,
                              FindCloneNodeCandidatePredicate& predicate)
{
    // Search for a suitable node
    return std::find_if(candidates.begin(),
                        candidates.end(),
                        [&predicate](const std::shared_ptr<Node>& nodePtr)
                        {
                            if (!nodePtr)
                                return false;

                            return predicate(*nodePtr);
                        });
}

Node* findCloneNodeCandidate(MegaClient& mc, const std::shared_ptr<SyncUpload_inClient>& upload)
{
    // Prepare the local extension.
    const std::string extLocal = std::invoke(
        [&mc](const LocalPath& localPath)
        {
            std::string extension;
            mc.fsaccess->getextension(localPath, extension);
            if (!extension.empty() && extension.front() == '.')
            {
                extension.erase(0, 1);
            }
            return extension;
        },
        upload->getLocalname());

    // Helper for node-based extension.
    const auto extractExtensionFromNode = [](const Node& node) -> std::string
    {
        std::string extension;
        node.getExtension(extension, node.displayname());
        if (!extension.empty() && extension.front() == '.')
        {
            extension.erase(0, 1);
        }
        return extension;
    };

    // Get candidate nodes.
    const auto candidates{mc.mNodeManager.getNodesByFingerprint(*upload)};

    // Construct the predicate.
    FindCloneNodeCandidatePredicate predicate{mc,
                                              upload,
                                              extLocal,
                                              std::move(extractExtensionFromNode)};

    // Find the candidate if the predicate matches.
    auto findCloneNodeCandidateIt = findCloneNodeCandidate_if(candidates, predicate);

    // CloneNode: nullptr if none found, or a valid pointer otherwise.
    Node* cloneNode =
        (findCloneNodeCandidateIt != std::end(candidates) && !predicate.mFoundCandidateHasZeroKey) ?
            (*findCloneNodeCandidateIt).get() :
            nullptr;

    return cloneNode;
}

/****************\
*  SYNC UPLOADS  *
\****************/

void clientUpload(MegaClient& mc,
                  TransferDbCommitter& committer,
                  const std::shared_ptr<SyncUpload_inClient>& upload,
                  const VersioningOption vo,
                  const bool queueFirst,
                  const NodeHandle ovHandleIfShortcut)
{
    assert(!upload->wasStarted);
    upload->wasStarted = true;

    // If we found a node to clone with a valid key, call putNodesToCloneNode.
    if (Node* cloneNode = findCloneNodeCandidate(mc, upload); cloneNode)
    {
        LOG_debug << "Cloning node rather than sync uploading: " << cloneNode->displaypath()
                  << " for " << upload->sourceLocalname;

        // completion function is supplied to putNodes command
        upload->sendPutnodesToCloneNode(&mc, ovHandleIfShortcut, cloneNode);
        upload->putnodesStarted = true;
        upload->wasCompleted = true;
        return;
    }

    // Otherwise, proceed with the normal upload.
    upload->tag = mc.nextreqtag();
    upload->selfKeepAlive = upload;
    mc.startxfer(PUT, upload.get(), committer, false, queueFirst, false, vo, nullptr, upload->tag);
}

void UploadThrottlingManager::addToDelayedQueue(DelayedSyncUpload&& delayedUpload)
{
    assert(mSyncs.onSyncThread());
    mDelayedQueue.emplace(std::move(delayedUpload));
}

void UploadThrottlingManager::setThrottleUpdateRate(const std::chrono::seconds interval)
{
    assert(mSyncs.onSyncThread());

    if (interval < std::chrono::seconds(MIN_PROCESS_INTERVAL_SECONDS) ||
        interval >= std::chrono::seconds(TIMEOUT_TO_RESET_UPLOAD_COUNTERS_SECONDS))
    {
        LOG_warn << "[UploadThrottle] Invalid throttle update rate (" << interval.count()
                 << " secs). Must be >= " << MIN_PROCESS_INTERVAL_SECONDS << " and < "
                 << TIMEOUT_TO_RESET_UPLOAD_COUNTERS_SECONDS << " seconds";
        return;
    }

    LOG_debug << "[UploadThrottle] Throttle update rate set to " << interval.count() << " secs";
    mThrottleUpdateRate = interval;
}

void UploadThrottlingManager::setMaxUploadsBeforeThrottle(const unsigned maxUploadsBeforeThrottle)
{
    assert(mSyncs.onSyncThread());

    if (maxUploadsBeforeThrottle < DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE)
    {
        LOG_warn << "[UploadThrottle] Invalid max uploads value (" << maxUploadsBeforeThrottle
                 << "). Must be >= " << DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE;
        return;
    }

    LOG_debug << "[UploadThrottle] Num uploads before throttle set to " << maxUploadsBeforeThrottle;
    mMaxUploadsBeforeThrottle = maxUploadsBeforeThrottle;
}

std::chrono::seconds UploadThrottlingManager::uploadCounterInactivityExpirationTime() const
{
    assert(mSyncs.onSyncThread());
    return mUploadCounterInactivityExpirationTime;
}

unsigned UploadThrottlingManager::maxUploadsBeforeThrottle() const
{
    assert(mSyncs.onSyncThread());
    return mMaxUploadsBeforeThrottle;
}

bool UploadThrottlingManager::checkProcessDelayedUploads() const
{
    assert(mSyncs.onSyncThread());

    if (mDelayedQueue.empty())
    {
        return false;
    }

    // Calculate adjusted interval
    const auto adjustedThrottleUpdateRate =
        std::max(std::chrono::seconds(MIN_PROCESS_INTERVAL_SECONDS),
                 std::chrono::duration_cast<std::chrono::seconds>(
                     mThrottleUpdateRate / std::sqrt(static_cast<double>(mDelayedQueue.size()))));

    if (auto timeSinceLastProcessedUpload = std::chrono::steady_clock::now() - mLastProcessedTime;
        timeSinceLastProcessedUpload < adjustedThrottleUpdateRate)
    {
        SYNCS_verbose_timed
            << "[UploadThrottle] Waiting to process delayed uploads [processing every "
            << adjustedThrottleUpdateRate.count() << " secs, time lapsed since last process: "
            << std::chrono::duration_cast<std::chrono::seconds>(timeSinceLastProcessedUpload)
                   .count()
            << " secs, delayed uploads = " << mDelayedQueue.size() << "]";
        return false;
    }

    return true;
}

void UploadThrottlingManager::processDelayedUploads()
{
    assert(mSyncs.onSyncThread());

    if (!checkProcessDelayedUploads())
    {
        return;
    }

    LOG_verbose << "[UploadThrottle] Processing delayed uploads. Queue size: "
                << mDelayedQueue.size();

    bool taskQueued{false};
    while (!mDelayedQueue.empty() && !taskQueued)
    {
        DelayedSyncUpload delayedUpload = std::move(mDelayedQueue.front());
        mDelayedQueue.pop();

        if (queueClientDelayedUpload(std::move(delayedUpload)))
        {
            taskQueued = true;
            mLastProcessedTime = std::chrono::steady_clock::now();
        }
    }
}

bool UploadThrottlingManager::queueClientDelayedUpload(DelayedSyncUpload&& delayedUpload)
{
    if (!delayedUpload.mWeakUpload.lock())
    {
        LOG_warn << "[UploadThrottle] Upload is no longer valid. Skipping this task";
        return false;
    }

    mSyncs.queueClient(
        [delayedUpload = std::move(delayedUpload)](MegaClient& mc, TransferDbCommitter& committer)
        {
            if (auto upload = delayedUpload.mWeakUpload.lock()) // Check again if upload is valid
            {
                LOG_debug << "[UploadThrottle] Sync - Sending delayed file "
                          << upload->getLocalname();
                clientUpload(mc,
                             committer,
                             upload,
                             delayedUpload.mVersioningOption,
                             delayedUpload.mQueueFirst,
                             delayedUpload.mOvHandleIfShortcut);
            }
            else
            {
                LOG_warn << "[UploadThrottle] Upload no longer valid inside queueClient";
            }
        });

    // We return true as the shared_ptr was valid when the we were going to add the task to the
    // client queue. Of course, it is theoretically possible that, right before returning this
    // 'true' value, the shared_ptr has already become invalid, even when we have just called
    // "queueClient()" without executing the lambda yet. But that doesn't matter: the important
    // check is the one inside the lambda. The pre-check before calling queueClient() is meant only
    // to avoid enqueueing the task when we know that the shared_ptr is not even valid at this
    // point.
    return true;
}

} // namespace mega

#endif // ENABLE_SYNC
