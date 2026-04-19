/**
 * @file SdkTestSyncVersionedNodeDeletion_test.cpp
 * @brief Reproduction tests for the zombie versioned nodes bug.
 *
 * When a delete action packet arrives for a versioned file, proctree() walks
 * the node tree via getChildren(). On the slow path of getChildren_internal()
 * (when mAllChildrenHandleLoaded == false) the DB query was silently dropping
 * version children, so TreeProcDel never visited older versions and they
 * survived as zombies -- never marked removed, and still available to the sync
 * engine as potential copy sources.
 *
 * These tests force the slow path via the locallogout + resumeSession +
 * fetchnodes sequence, then delete a versioned file and assert either that all
 * version nodes are properly cleaned up or, for the sync upload regression, that
 * deleted versions are not reused as clone copy sources.
 */

#ifdef ENABLE_SYNC

#include "integration_test_utils.h"
#include "mega/testhooks.h"
#include "mega/utils.h"
#include "megautils.h"
#include "sdk_test_utils.h"
#include "SdkTestSyncNodesOperations.h"

#include <algorithm>
#include <mutex>

using namespace sdk_test;
using namespace testing;

namespace
{
std::string base64NodeHandle(MegaHandle handle)
{
    return Base64Str<MegaClient::NODEHANDLE>(handle).chars;
}

std::string platformEncodedAbsolutePath(const fs::path& path)
{
    return LocalPath::fromAbsolutePath(path_u8string(path)).platformEncoded();
}

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
class SyncUploadRequestObserver
{
public:
    SyncUploadRequestObserver():
        mPreviousHook{globalMegaTestHooks.onHttpReqPost}
    {
        globalMegaTestHooks.onHttpReqPost = [this](HttpReq* req)
        {
            const bool handled = mPreviousHook && mPreviousHook(req);
            handleRequest(req);
            return handled;
        };
    }

    ~SyncUploadRequestObserver()
    {
        globalMegaTestHooks.onHttpReqPost = std::move(mPreviousHook);
    }

    bool hasPutnodesSource() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return !mPutnodesSources.empty();
    }

    size_t uploadRequestCount() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mUploadRequestCount;
    }

    // Count of putnodes whose source "h" is an upload token (fresh wsupload completion)
    // rather than an existing node handle (clone). See handleRequest() for classification.
    size_t freshUploadCount() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mFreshUploadCount;
    }

    bool usedPutnodesSource(const std::string& source) const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return std::find(mPutnodesSources.begin(), mPutnodesSources.end(), source) !=
               mPutnodesSources.end();
    }

    std::string summary() const
    {
        std::lock_guard<std::mutex> lock(mMutex);

        std::string result = "upload requests: " + std::to_string(mUploadRequestCount) +
                             ", fresh uploads (wsupload): " + std::to_string(mFreshUploadCount) +
                             ", putnodes sources:";
        if (mPutnodesSources.empty())
        {
            result += " <none>";
            return result;
        }

        for (const auto& source: mPutnodesSources)
        {
            result += " ";
            result += source;
        }

        return result;
    }

private:
    static size_t countToken(const std::string& payload, const std::string& token)
    {
        size_t count = 0;
        size_t pos = 0;
        while ((pos = payload.find(token, pos)) != std::string::npos)
        {
            ++count;
            pos += token.size();
        }
        return count;
    }

    static std::vector<std::string> extractPutnodesSources(const std::string& payload)
    {
        static const std::string putnodesToken = "\"a\":\"p\"";
        static const std::string newNodeSourceToken = "\"n\":[{\"h\":\"";

        std::vector<std::string> sources;
        size_t commandPos = 0;
        while ((commandPos = payload.find(putnodesToken, commandPos)) != std::string::npos)
        {
            auto sourcePos = payload.find(newNodeSourceToken, commandPos);
            if (sourcePos == std::string::npos)
            {
                commandPos += putnodesToken.size();
                continue;
            }

            sourcePos += newNodeSourceToken.size();
            const auto sourceEnd = payload.find('"', sourcePos);
            if (sourceEnd == std::string::npos)
            {
                break;
            }

            sources.emplace_back(payload.substr(sourcePos, sourceEnd - sourcePos));
            commandPos = sourceEnd;
        }

        return sources;
    }

    void handleRequest(HttpReq* req)
    {
        if (!req || !req->out)
        {
            return;
        }

        const std::string& payload = *req->out;
        static const std::string uploadToken = "\"a\":\"u\"";

        const auto putnodesSources = extractPutnodesSources(payload);
        const auto uploadCount = countToken(payload, uploadToken);

        if (putnodesSources.empty() && uploadCount == 0)
        {
            return;
        }

        // Putnodes "n":[{"h":"..."}] encodes either a clone source node handle
        // (NODEHANDLE bytes -> Base64Str<NODEHANDLE>::STRLEN chars) or an upload
        // token from a wsupload completion (UPLOADTOKENLEN bytes ->
        // Base64Str<UPLOADTOKENLEN>::STRLEN chars). See src/commands.cpp where
        // CommandPutNodes switches on NEW_NODE vs NEW_UPLOAD. Treat the latter as
        // a fresh upload for the purposes of this test, because the wsupload path
        // never emits the legacy "a":"u" HTTP command.
        size_t freshUploads = 0;
        for (const auto& source: putnodesSources)
        {
            if (source.size() == Base64Str<UPLOADTOKENLEN>::STRLEN)
            {
                ++freshUploads;
            }
        }

        std::lock_guard<std::mutex> lock(mMutex);
        mUploadRequestCount += uploadCount;
        mFreshUploadCount += freshUploads;
        mPutnodesSources.insert(mPutnodesSources.end(),
                                putnodesSources.begin(),
                                putnodesSources.end());
    }

    std::function<bool(HttpReq*)> mPreviousHook;
    mutable std::mutex mMutex;
    size_t mUploadRequestCount{0};
    size_t mFreshUploadCount{0};
    std::vector<std::string> mPutnodesSources;
};
#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED
} // namespace

/**
 * @class SdkTestSyncVersionedNodeDeletion
 * @brief Test fixture that sets up a synced subfolder containing a single file,
 *        which is then versioned during the test body.
 *
 * The initial cloud tree created by getElements() is:
 *   dir1/
 *     subdir/
 *       versionedFile.txt
 *
 * The file lives at depth >= 2 (under a subfolder of the sync root) so that
 * after locallogout + resumeSession + fetchnodes, the subfolder's
 * mAllChildrenHandleLoaded is false and getChildren() takes the slow path.
 */
class SdkTestSyncVersionedNodeDeletion: public SdkTestSyncNodesOperations
{
public:
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> ELEMENTS{
            DirNodeInfo(DEFAULT_SYNC_REMOTE_PATH)
                .addChild(DirNodeInfo("subdir").addChild(FileNodeInfo("versionedFile.txt")))};
        return ELEMENTS;
    }

    const std::string& getRootTestDir() const override
    {
        static const std::string dirName{"SDK_TEST_SYNC_VERSIONED_NODE_DELETION_AUX_DIR"};
        return dirName;
    }

    /**
     * @brief Upload new content for the same file name into the given parent folder,
     *        creating a new version of the file.
     *
     * @param parentNode The parent folder node
     * @param fileName   The remote file name (must match the existing file name to version)
     * @param version    A version counter used to generate unique local content
     * @param outHandle  If non-null, receives the handle of the uploaded node
     */
    void uploadVersion(MegaNode* parentNode,
                       const std::string& fileName,
                       int version,
                       MegaHandle* outHandle = nullptr)
    {
        // Create a local temp file with unique content for this version
        const std::string localName = fileName + "_v" + std::to_string(version);
        const std::string content = "version_content_" + std::to_string(version);
        createFile(localName, false, content);

        MegaHandle fh = INVALID_HANDLE;
        ASSERT_EQ(MegaError::API_OK,
                  doStartUpload(0,
                                &fh,
                                localName.c_str(),
                                parentNode,
                                fileName.c_str() /*fileName -- same name creates a version*/,
                                ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr /*appData*/,
                                false /*isSourceTemporary*/,
                                false /*startFirst*/,
                                nullptr /*cancelToken*/))
            << "Failed to upload version " << version << " of " << fileName;

        deleteFile(localName);

        if (outHandle)
        {
            *outHandle = fh;
        }
    }
};

/**
 * @brief SdkTestSyncVersionedNodeDeletion.DeleteVersionedFileRemovesAllVersions
 *
 * Verify that when a versioned file is deleted remotely, ALL version nodes are
 * properly removed from the SDK's internal state -- even when the slow path of
 * getChildren_internal() is taken (mAllChildrenHandleLoaded == false).
 *
 * Phase A (setup):
 *   1. Wait for sync to stabilize with the initial tree (subdir/versionedFile.txt)
 *   2. Create 3 versions by uploading new content to the same file name
 *   3. Verify versions exist using hasVersions() and getVersions()
 *   4. Record handles of ALL version nodes
 *   5. Save the session string
 *
 * Phase B (test with slow path):
 *   6. Perform locallogout(0)
 *   7. Perform resumeSession(session) + fetchnodes(0) to rebuild node tree from DB
 *      --> non-root nodes now have mAllChildrenHandleLoaded == false (slow path active)
 *   8. Delete the versioned file via doDeleteNode
 *   9. Wait for the action packet to be processed (wait for sync to stabilize)
 *  10. ASSERT: all previously-recorded version handles should resolve to nullptr
 *  11. ASSERT: the local sync directory should no longer contain the file
 *
 * Expected result with current buggy code: FAIL -- zombie version nodes survive
 * because the slow path filters them out during proctree() traversal.
 */
TEST_F(SdkTestSyncVersionedNodeDeletion, DeleteVersionedFileRemovesAllVersions)
{
    const auto logPre = getLogPrefix();

    // =========================================================================
    // Phase A: Setup -- create versions and record state
    // =========================================================================

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    // Get the parent subfolder node for uploads
    LOG_verbose << logPre << "Getting subdir node for version uploads";
    auto subdirNode = getNodeByPath("dir1/subdir");
    ASSERT_TRUE(subdirNode) << "Subfolder 'subdir' not found in cloud";

    // Upload 3 versions of the file (the initial upload already exists as v1)
    constexpr int numExtraVersions = 3;
    MegaHandle latestFileHandle = INVALID_HANDLE;
    for (int i = 1; i <= numExtraVersions; ++i)
    {
        LOG_verbose << logPre << "Uploading version " << i << " of versionedFile.txt";
        ASSERT_NO_FATAL_FAILURE(
            uploadVersion(subdirNode.get(), "versionedFile.txt", i, &latestFileHandle));
    }
    ASSERT_NE(latestFileHandle, INVALID_HANDLE) << "No handle returned for the latest version";

    // Wait for sync to stabilize after version uploads
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    // Verify versions exist
    LOG_verbose << logPre << "Verifying versions exist";
    std::unique_ptr<MegaNode> latestNode(megaApi[0]->getNodeByHandle(latestFileHandle));
    ASSERT_TRUE(latestNode) << "Latest versioned node not found";
    ASSERT_TRUE(megaApi[0]->hasVersions(latestNode.get()))
        << "File should have versions after multiple uploads";

    // Get and record ALL version handles
    std::unique_ptr<MegaNodeList> allVersions(megaApi[0]->getVersions(latestNode.get()));
    ASSERT_TRUE(allVersions) << "getVersions returned null";
    const int totalVersions = allVersions->size();
    ASSERT_GE(totalVersions, 2) << "Expected at least 2 versions (original + uploads)";
    LOG_verbose << logPre << "Total versions: " << totalVersions;

    std::vector<MegaHandle> versionHandles;
    versionHandles.reserve(static_cast<size_t>(totalVersions));
    for (int i = 0; i < totalVersions; ++i)
    {
        const auto* vNode = allVersions->get(i);
        ASSERT_TRUE(vNode) << "Version node at index " << i << " is null";
        versionHandles.push_back(vNode->getHandle());
        LOG_verbose << logPre << "  Version " << i
                    << " handle: " << base64NodeHandle(vNode->getHandle());
    }

    // Save session before locallogout
    LOG_verbose << logPre << "Saving session string";
    std::unique_ptr<char[]> session(dumpSession(0));
    ASSERT_TRUE(session) << "Failed to dump session";

    // We must remove the sync BEFORE locallogout so it can be re-created after
    LOG_verbose << logPre << "Removing sync before locallogout";
    ASSERT_TRUE(removeSync(megaApi[0].get(), mBackupId));
    mBackupId = UNDEF; // Prevent TearDown from trying to remove it again

    // =========================================================================
    // Phase B: Force slow path and delete the versioned file
    // =========================================================================

    // locallogout clears all in-memory nodes (including mAllChildrenHandleLoaded flags)
    LOG_verbose << logPre << "Performing locallogout";
    ASSERT_NO_FATAL_FAILURE(locallogout(0));

    // resumeSession + fetchnodes rebuilds node tree from DB
    // After this, non-root nodes have mAllChildrenHandleLoaded == false (slow path)
    LOG_verbose << logPre << "Resuming session and fetching nodes (slow path now active)";
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get(), 0));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    // Verify we can still find the latest node after session restore
    latestNode.reset(megaApi[0]->getNodeByHandle(latestFileHandle));
    ASSERT_TRUE(latestNode) << "Latest versioned node not found after session restore";

    // Delete the versioned file -- this triggers sc_deltree -> proctree -> getChildren.
    // The bug being regressed against: on the slow path, the DB-backed getChildren
    // used to filter out version children, so proctree() never visited them.
    LOG_verbose << logPre << "Deleting the versioned file (slow path active)";
    ASSERT_EQ(API_OK, doDeleteNode(0, latestNode.get())) << "Failed to delete the versioned file";

    // Wait for the deletion action packet to be processed
    // Give sufficient time for the SDK to process the action packet
    LOG_verbose << logPre << "Waiting for deletion to propagate";
    const auto allVersionsGone = [this, &versionHandles]() -> bool
    {
        for (const auto& handle: versionHandles)
        {
            std::unique_ptr<MegaNode> node(megaApi[0]->getNodeByHandle(handle));
            if (node)
            {
                return false; // At least one version still exists
            }
        }
        return true; // All versions are gone
    };
    // Wait up to COMMON_TIMEOUT for all version handles to resolve to nullptr
    const bool versionsCleanedUp = waitFor(allVersionsGone, COMMON_TIMEOUT, 5s);

    // ASSERT: all version handles must resolve to nullptr
    // With the current buggy code, this will FAIL because zombie version nodes survive
    for (size_t i = 0; i < versionHandles.size(); ++i)
    {
        std::unique_ptr<MegaNode> node(megaApi[0]->getNodeByHandle(versionHandles[i]));
        EXPECT_FALSE(node) << "ZOMBIE NODE DETECTED: Version " << i << " with handle "
                           << base64NodeHandle(versionHandles[i])
                           << " still exists after deletion. This is the zombie node bug -- "
                           << "the slow path of getChildren_internal() filtered out this version "
                           << "node during proctree() traversal.";
    }

    ASSERT_TRUE(versionsCleanedUp)
        << "Not all version nodes were removed after deleting the versioned file. "
        << "Zombie version nodes persist because the slow path of "
        << "getChildren_internal() dropped version children from its DB query.";

    // ASSERT: the local file should no longer exist
    // (Without the sync being active after locallogout, we verify cloud state only --
    //  the local file check is relevant if we re-enable the sync, but the primary assertion
    //  is that version nodes are gone from the SDK's internal state.)
    LOG_verbose << logPre << "All version nodes successfully removed -- test passed";
}

/**
 * @brief
 * SdkTestSyncVersionedNodeDeletion.DeleteVersionedFileRemovesVersionsAndSyncsReplacementCloudFile
 *
 * Verify that after deleting a versioned file (with slow path active), the sync
 * engine still propagates the remote deletion locally, and later syncs down a
 * replacement cloud file uploaded outside the sync.
 *
 * Phase A (setup):
 *   1. Same as Test 1: create versioned file in subdir, save session
 *
 * Phase B (test with slow path):
 *   2. locallogout + resumeSession + fetchnodes (slow path active)
 *   3. Re-create the sync
 *   4. Delete the versioned file remotely
 *   5. Wait for sync to stabilize
 *   6. Upload a NEW cloud file with the same name in the same folder
 *   7. Wait for sync to complete
 *   8. ASSERT: new file exists in cloud AND locally
 *   9. ASSERT: no sync stall or error state
 *  10. ASSERT: sync is running (not stalled, not errored)
 *
 * Expected result with current buggy code: FAIL -- version zombies persist,
 * the deletion does not cleanly propagate, and the replacement cloud file does
 * not settle correctly through the sync.
 */
TEST_F(SdkTestSyncVersionedNodeDeletion,
       DeleteVersionedFileRemovesVersionsAndSyncsReplacementCloudFile)
{
    const auto logPre = getLogPrefix();

    // =========================================================================
    // Phase A: Setup -- create versions and record state
    // =========================================================================

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    // Get the parent subfolder node for uploads
    LOG_verbose << logPre << "Getting subdir node for version uploads";
    auto subdirNode = getNodeByPath("dir1/subdir");
    ASSERT_TRUE(subdirNode) << "Subfolder 'subdir' not found in cloud";

    // Upload 3 versions of the file
    constexpr int numExtraVersions = 3;
    MegaHandle latestFileHandle = INVALID_HANDLE;
    for (int i = 1; i <= numExtraVersions; ++i)
    {
        LOG_verbose << logPre << "Uploading version " << i << " of versionedFile.txt";
        ASSERT_NO_FATAL_FAILURE(
            uploadVersion(subdirNode.get(), "versionedFile.txt", i, &latestFileHandle));
    }
    ASSERT_NE(latestFileHandle, INVALID_HANDLE) << "No handle returned for the latest version";

    // Wait for sync to stabilize
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    // Verify versions exist
    LOG_verbose << logPre << "Verifying versions exist";
    std::unique_ptr<MegaNode> latestNode(megaApi[0]->getNodeByHandle(latestFileHandle));
    ASSERT_TRUE(latestNode) << "Latest versioned node not found";
    ASSERT_TRUE(megaApi[0]->hasVersions(latestNode.get()))
        << "File should have versions after multiple uploads";

    // Record version handles for later verification
    std::unique_ptr<MegaNodeList> allVersions(megaApi[0]->getVersions(latestNode.get()));
    ASSERT_TRUE(allVersions) << "getVersions returned null";
    const int totalVersions = allVersions->size();
    LOG_verbose << logPre << "Total versions: " << totalVersions;

    std::vector<MegaHandle> versionHandles;
    versionHandles.reserve(static_cast<size_t>(totalVersions));
    for (int i = 0; i < totalVersions; ++i)
    {
        versionHandles.push_back(allVersions->get(i)->getHandle());
    }

    // Save session before locallogout
    LOG_verbose << logPre << "Saving session string";
    std::unique_ptr<char[]> session(dumpSession(0));
    ASSERT_TRUE(session) << "Failed to dump session";

    // Record the local sync path for later verification
    const auto localSyncDir = getLocalTmpDirU8string();

    // Remove sync before locallogout
    LOG_verbose << logPre << "Removing sync before locallogout";
    ASSERT_TRUE(removeSync(megaApi[0].get(), mBackupId));
    mBackupId = UNDEF;

    // =========================================================================
    // Phase B: Force slow path, delete file, create new file, verify sync
    // =========================================================================

    LOG_verbose << logPre << "Performing locallogout";
    ASSERT_NO_FATAL_FAILURE(locallogout(0));

    LOG_verbose << logPre << "Resuming session and fetching nodes (slow path now active)";
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get(), 0));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    // Re-create the sync after session restoration
    LOG_verbose << logPre << "Re-creating sync on dir1";
    auto dir1Node = getNodeByPath("dir1");
    ASSERT_TRUE(dir1Node) << "dir1 not found after session restore";
    mBackupId = sdk_test::syncFolder(megaApi[0].get(), localSyncDir, dir1Node->getHandle());
    ASSERT_NE(mBackupId, UNDEF) << "Failed to re-create sync after session restore";

    // Wait for sync to be running
    LOG_verbose << logPre << "Waiting for sync to be running";
    {
        auto syncState = waitForSyncState(megaApi[0].get(),
                                          mBackupId,
                                          MegaSync::RUNSTATE_RUNNING,
                                          MegaSync::NO_SYNC_ERROR);
        ASSERT_TRUE(syncState) << "Sync did not reach RUNNING state after session restore";
    }

    // Wait for the sync engine to complete its initial scan of all directories.
    // waitForSyncToMatchCloudAndLocalExhaustive() only checks external state (cloud
    // names/fingerprints match local names/fingerprints from the test thread). The sync
    // engine may still be scanning subdirectories and establishing internal
    // syncedCloudNodeHandle mappings needed for deletion propagation. We must detect the
    // full scan lifecycle: not-scanning → scanning → not-scanning. Checking only
    // !isScanning() can return true BEFORE the scan starts (the flag is global and may
    // be false between syncs), so we first wait for scanning to begin, then for it to end.
    LOG_verbose << logPre << "Waiting for sync engine to finish initial scan";
    const auto syncIsScanning = [this]() -> bool
    {
        return megaApi[0]->isScanning();
    };
    // Detect scan start (best effort: short polling catches scans lasting >50ms)
    waitFor(syncIsScanning, 10s, std::chrono::milliseconds(50));
    // Now wait for the scan to complete
    const auto syncScanDone = [this]() -> bool
    {
        return !megaApi[0]->isScanning();
    };
    ASSERT_TRUE(waitFor(syncScanDone, COMMON_TIMEOUT, 1s))
        << "Sync engine did not finish initial scan within timeout";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    const fs::path localVersionedFilePath = getLocalTmpDir() / "subdir" / "versionedFile.txt";
    const auto localFileIsSynced = [this, localVersionedFilePath]() -> bool
    {
        std::string path = platformEncodedAbsolutePath(localVersionedFilePath);
        return megaApi[0]->syncPathState(&path) == MegaApi::STATE_SYNCED;
    };
    ASSERT_TRUE(waitFor(localFileIsSynced, COMMON_TIMEOUT, 1s))
        << "The re-created sync did not report the local file as STATE_SYNCED before deletion";

    // Verify we can still find the file after session restore
    latestNode.reset(megaApi[0]->getNodeByHandle(latestFileHandle));
    ASSERT_TRUE(latestNode) << "Latest versioned node not found after session restore";

    // Delete the versioned file -- triggers sc_deltree on the slow path
    LOG_verbose << logPre << "Deleting the versioned file (slow path active)";
    ASSERT_EQ(API_OK, doDeleteNode(0, latestNode.get())) << "Failed to delete the versioned file";

    // Early zombie check: verify all version nodes are cleaned up after deletion,
    // independently of sync propagation. This catches the zombie bug even if sync
    // propagation were to fail for unrelated reasons (network timeout, CI slowness).
    LOG_verbose << logPre << "Checking for zombie version nodes after deletion";
    const auto allVersionsGone = [this, &versionHandles]() -> bool
    {
        for (const auto& handle: versionHandles)
        {
            std::unique_ptr<MegaNode> node(megaApi[0]->getNodeByHandle(handle));
            if (node)
            {
                return false;
            }
        }
        return true;
    };
    const bool versionsCleanedUp = waitFor(allVersionsGone, COMMON_TIMEOUT, 5s);
    for (size_t i = 0; i < versionHandles.size(); ++i)
    {
        std::unique_ptr<MegaNode> zombieCheck(megaApi[0]->getNodeByHandle(versionHandles[i]));
        EXPECT_FALSE(zombieCheck) << "ZOMBIE NODE DETECTED (early check): Version " << i
                                  << " with handle " << base64NodeHandle(versionHandles[i])
                                  << " still exists after deletion. The slow path of "
                                  << "getChildren_internal() filtered out this version node "
                                  << "during proctree() traversal.";
    }
    ASSERT_TRUE(versionsCleanedUp)
        << "Not all version nodes were removed after deleting the versioned file. "
        << "Zombie version nodes persist because the slow path of "
        << "getChildren_internal() dropped version children from its DB query.";

    // Wait for deletion to propagate through sync
    LOG_verbose << logPre << "Waiting for deletion to propagate through sync";
    const auto fileGoneLocally = [this]() -> bool
    {
        const auto localFilePath = getLocalTmpDir() / "subdir" / "versionedFile.txt";
        return !fs::exists(localFilePath);
    };
    ASSERT_TRUE(waitFor(fileGoneLocally, COMMON_TIMEOUT, 5s))
        << "The local file was not removed after cloud deletion. "
        << "Zombie version nodes may be interfering with sync.";

    // Now create a NEW file with the same name in the cloud subfolder
    LOG_verbose << logPre << "Creating a new file with the same name in the cloud";
    subdirNode = getNodeByPath("dir1/subdir");
    ASSERT_TRUE(subdirNode) << "Subfolder 'subdir' not found after deletion";

    // Upload a fresh file with the same name
    const std::string newContent = "completely_new_content_after_deletion";
    const std::string localNewFileName = "versionedFile_new.txt";
    createFile(localNewFileName, false, newContent);
    MegaHandle newFileHandle = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &newFileHandle,
                            localNewFileName.c_str(),
                            subdirNode.get(),
                            "versionedFile.txt" /*same remote name*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr,
                            false,
                            false,
                            nullptr))
        << "Failed to upload new file with the same name";
    deleteFile(localNewFileName);
    ASSERT_NE(newFileHandle, INVALID_HANDLE) << "No handle for the new file";

    // Wait for sync to stabilize with the new file
    LOG_verbose << logPre << "Waiting for sync to stabilize with the new file";
    const auto newFileExistsLocally = [this]() -> bool
    {
        const auto localFilePath = getLocalTmpDir() / "subdir" / "versionedFile.txt";
        return fs::exists(localFilePath);
    };
    ASSERT_TRUE(waitFor(newFileExistsLocally, COMMON_TIMEOUT, 5s))
        << "New file did not appear locally after upload. "
        << "Zombie nodes may be causing sync to stall.";

    // ASSERT: the new file exists in the cloud
    LOG_verbose << logPre << "Verifying new file exists in cloud";
    std::unique_ptr<MegaNode> newNode(megaApi[0]->getNodeByHandle(newFileHandle));
    ASSERT_TRUE(newNode) << "New file node not found in cloud";
    ASSERT_STREQ(newNode->getName(), "versionedFile.txt")
        << "New file has unexpected name in cloud";

    // Verify the new file is independent of the deleted version chain.
    // If zombie nodes interfered with the sync/upload, the new file might
    // have been linked to the old version chain or created via copy-from-zombie.
    for (const auto& zombieHandle: versionHandles)
    {
        EXPECT_NE(newFileHandle, zombieHandle)
            << "New file handle matches zombie version handle " << base64NodeHandle(zombieHandle)
            << " -- the new file may have been created by copying from a zombie node.";
    }
    EXPECT_FALSE(megaApi[0]->hasVersions(newNode.get()))
        << "New file unexpectedly has versions. Zombie version nodes from the deleted "
        << "file may have been linked to the new file as a copy source.";

    // ASSERT: the new file exists locally
    const auto localFilePath = getLocalTmpDir() / "subdir" / "versionedFile.txt";
    ASSERT_TRUE(fs::exists(localFilePath)) << "New file not found locally at " << localFilePath;

    // ASSERT: sync is running without errors
    LOG_verbose << logPre << "Verifying sync is running without errors";
    {
        std::unique_ptr<MegaSync> sync(megaApi[0]->getSyncByBackupId(mBackupId));
        ASSERT_TRUE(sync) << "Sync object not found";
        EXPECT_EQ(sync->getRunState(), MegaSync::RUNSTATE_RUNNING)
            << "Sync is not in RUNNING state. Zombie version nodes may have caused "
            << "the sync to stall or error. RunState: " << sync->getRunState()
            << ", Error: " << sync->getError();
        EXPECT_EQ(sync->getError(), MegaSync::NO_SYNC_ERROR)
            << "Sync has an error. Zombie nodes may be interfering with operations.";
    }

    // ASSERT: no zombie version nodes remain
    LOG_verbose << logPre << "Verifying no zombie version nodes remain";
    for (size_t i = 0; i < versionHandles.size(); ++i)
    {
        std::unique_ptr<MegaNode> zombieCheck(megaApi[0]->getNodeByHandle(versionHandles[i]));
        EXPECT_FALSE(zombieCheck) << "ZOMBIE NODE DETECTED: Old version " << i << " with handle "
                                  << base64NodeHandle(versionHandles[i])
                                  << " still exists after deletion and re-creation. "
                                  << "This zombie may be used as a copy source by the sync engine.";
    }

    LOG_verbose << logPre << "Test completed";
}

/**
 * @brief SdkTestSyncVersionedNodeDeletion.SyncDoesNotUseDeletedVersionedNodeAsCopySource
 *
 * Verify the real copy-source regression:
 *   1. Create several versions of a synced file and remember the handle of one
 *      non-latest version with known content.
 *   2. Force the slow path via locallogout + resumeSession + fetchnodes.
 *   3. Re-create the sync, delete the versioned cloud file, and wait until the
 *      local synced file is removed.
 *   4. Create a replacement file locally inside the sync root, using the exact
 *      content of the remembered deleted version so clone-candidate lookup has
 *      a matching fingerprint.
 *   5. Trace outgoing requests and ASSERT that the sync never sends putnodes
 *      using that deleted version handle as the clone source.
 *   6. ASSERT that the post-fix path falls back to a normal upload instead.
 *
 * Expected result with buggy code: FAIL -- a deleted version zombie is still
 * returned by clone-candidate lookup and its handle appears as the putnodes
 * source (`"a":"p"` / `"h":"<deleted-version-handle>"`).
 */
TEST_F(SdkTestSyncVersionedNodeDeletion, SyncDoesNotUseDeletedVersionedNodeAsCopySource)
{
#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const auto logPre = getLogPrefix();

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    LOG_verbose << logPre << "Getting subdir node for version uploads";
    auto subdirNode = getNodeByPath("dir1/subdir");
    ASSERT_TRUE(subdirNode) << "Subfolder 'subdir' not found in cloud";

    constexpr int numExtraVersions = 3;
    constexpr int copiedVersionNumber = 2;
    static const std::string copiedVersionContent = "version_content_2";

    MegaHandle latestFileHandle = INVALID_HANDLE;
    std::vector<MegaHandle> uploadedVersionHandles;
    uploadedVersionHandles.reserve(numExtraVersions);
    for (int i = 1; i <= numExtraVersions; ++i)
    {
        MegaHandle uploadedHandle = INVALID_HANDLE;
        LOG_verbose << logPre << "Uploading version " << i << " of versionedFile.txt";
        ASSERT_NO_FATAL_FAILURE(
            uploadVersion(subdirNode.get(), "versionedFile.txt", i, &uploadedHandle));
        ASSERT_NE(uploadedHandle, INVALID_HANDLE) << "Upload " << i << " did not return a handle";
        uploadedVersionHandles.push_back(uploadedHandle);
        latestFileHandle = uploadedHandle;
    }
    ASSERT_NE(latestFileHandle, INVALID_HANDLE) << "No handle returned for the latest version";

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    const MegaHandle deletedVersionHandle =
        uploadedVersionHandles.at(static_cast<size_t>(copiedVersionNumber - 1));
    const std::string deletedVersionHandleBase64 = base64NodeHandle(deletedVersionHandle);

    LOG_verbose << logPre << "Verifying versions exist and include the copy-source candidate";
    std::unique_ptr<MegaNode> latestNode(megaApi[0]->getNodeByHandle(latestFileHandle));
    ASSERT_TRUE(latestNode) << "Latest versioned node not found";
    ASSERT_TRUE(megaApi[0]->hasVersions(latestNode.get()))
        << "File should have versions after multiple uploads";

    std::unique_ptr<MegaNodeList> allVersions(megaApi[0]->getVersions(latestNode.get()));
    ASSERT_TRUE(allVersions) << "getVersions returned null";

    bool deletedHandleFoundAmongVersions = false;
    for (int i = 0; i < allVersions->size(); ++i)
    {
        if (allVersions->get(i) && allVersions->get(i)->getHandle() == deletedVersionHandle)
        {
            deletedHandleFoundAmongVersions = true;
            break;
        }
    }
    ASSERT_TRUE(deletedHandleFoundAmongVersions)
        << "Deleted version handle candidate " << deletedVersionHandleBase64
        << " was not found in the version chain before deletion";

    LOG_verbose << logPre << "Saving session string";
    std::unique_ptr<char[]> session(dumpSession(0));
    ASSERT_TRUE(session) << "Failed to dump session";

    const auto localSyncDir = getLocalTmpDirU8string();

    LOG_verbose << logPre << "Removing sync before locallogout";
    ASSERT_TRUE(removeSync(megaApi[0].get(), mBackupId));
    mBackupId = UNDEF;

    LOG_verbose << logPre << "Performing locallogout";
    ASSERT_NO_FATAL_FAILURE(locallogout(0));

    LOG_verbose << logPre << "Resuming session and fetching nodes (slow path now active)";
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get(), 0));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    LOG_verbose << logPre << "Re-creating sync on dir1";
    auto dir1Node = getNodeByPath("dir1");
    ASSERT_TRUE(dir1Node) << "dir1 not found after session restore";
    mBackupId = sdk_test::syncFolder(megaApi[0].get(), localSyncDir, dir1Node->getHandle());
    ASSERT_NE(mBackupId, UNDEF) << "Failed to re-create sync after session restore";

    LOG_verbose << logPre << "Waiting for sync to be running";
    {
        auto syncState = waitForSyncState(megaApi[0].get(),
                                          mBackupId,
                                          MegaSync::RUNSTATE_RUNNING,
                                          MegaSync::NO_SYNC_ERROR);
        ASSERT_TRUE(syncState) << "Sync did not reach RUNNING state after session restore";
    }

    LOG_verbose << logPre << "Waiting for sync engine to finish initial scan";
    const auto syncIsScanning = [this]() -> bool
    {
        return megaApi[0]->isScanning();
    };
    waitFor(syncIsScanning, 10s, std::chrono::milliseconds(50));

    const auto syncScanDone = [this]() -> bool
    {
        return !megaApi[0]->isScanning();
    };
    ASSERT_TRUE(waitFor(syncScanDone, COMMON_TIMEOUT, 1s))
        << "Sync engine did not finish initial scan within timeout";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    const fs::path localVersionedFilePath = getLocalTmpDir() / "subdir" / "versionedFile.txt";
    const auto localFileIsSynced = [this, localVersionedFilePath]() -> bool
    {
        std::string path = platformEncodedAbsolutePath(localVersionedFilePath);
        return megaApi[0]->syncPathState(&path) == MegaApi::STATE_SYNCED;
    };
    ASSERT_TRUE(waitFor(localFileIsSynced, COMMON_TIMEOUT, 1s))
        << "The re-created sync did not report the local file as STATE_SYNCED before deletion";

    latestNode.reset(megaApi[0]->getNodeByHandle(latestFileHandle));
    ASSERT_TRUE(latestNode) << "Latest versioned node not found after session restore";

    LOG_verbose << logPre << "Deleting the versioned file (slow path active)";
    ASSERT_EQ(API_OK, doDeleteNode(0, latestNode.get())) << "Failed to delete the versioned file";

    LOG_verbose << logPre << "Waiting for deletion to propagate through sync";
    const auto fileGoneRemotely = [this]() -> bool
    {
        return !getNodeByPath("dir1/subdir/versionedFile.txt");
    };
    ASSERT_TRUE(waitFor(fileGoneRemotely, COMMON_TIMEOUT, 1s))
        << "Cloud file still exists after deletion";

    const auto fileGoneLocally = [this]() -> bool
    {
        const auto localFilePath = getLocalTmpDir() / "subdir" / "versionedFile.txt";
        return !fs::exists(localFilePath);
    };
    ASSERT_TRUE(waitFor(fileGoneLocally, COMMON_TIMEOUT, 5s))
        << "The local file was not removed after cloud deletion";

    SyncUploadRequestObserver observer;

    LOG_verbose << logPre << "Creating a local replacement file inside the sync root";
    const fs::path localReplacementPath = getLocalTmpDir() / "subdir" / "versionedFile.txt";
    ASSERT_TRUE(createFile(path_u8string(localReplacementPath), false, copiedVersionContent))
        << "Failed to create replacement file " << path_u8string(localReplacementPath);

    const auto putnodesObserved = [&observer]() -> bool
    {
        return observer.hasPutnodesSource();
    };
    ASSERT_TRUE(waitFor(putnodesObserved, COMMON_TIMEOUT, 1s))
        << "No putnodes request was captured for the sync upload path";

    ASSERT_FALSE(observer.usedPutnodesSource(deletedVersionHandleBase64))
        << "Sync attempted to clone from deleted version handle " << deletedVersionHandleBase64
        << ". Captured requests: " << observer.summary();

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

#ifndef MEGA_USE_WSUPLOAD
    EXPECT_GT(observer.uploadRequestCount(), 0u)
        << "Expected an upload request after deletion. Captured requests: " << observer.summary();
#endif

    EXPECT_GT(observer.freshUploadCount(), 0u)
        << "Expected a normal putnodes after deletion instead of a clone-only path. Captured "
           "requests: "
        << observer.summary();

    auto replacementNode = getNodeByPath("dir1/subdir/versionedFile.txt");
    ASSERT_TRUE(replacementNode) << "Replacement file not found in cloud after sync";
    EXPECT_FALSE(megaApi[0]->hasVersions(replacementNode.get()))
        << "Replacement file unexpectedly has versions after being re-created locally";

    LOG_verbose << logPre << "Test completed";
#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED
}

#endif // ENABLE_SYNC
