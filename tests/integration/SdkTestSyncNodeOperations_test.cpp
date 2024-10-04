/**
 * @file
 * @brief This file is expected to contain tests involving syncs and operations with nodes (local
 * and remote), e.g., what happens when the remote root of a sync gets deleted.
 */

#include "integration_test_utils.h"
#include "SdkTestNodesSetUp_test.h"

using namespace sdk_test;

/**
 * @class SdkTestSyncNodeOperations
 * @brief Test fixture designed to test operations involving node operations and syncs
 *
 * @note As a reminder, everything is done inside the remote node named by getRootTestDir() which
 * means that all the methods involving a remote "path" are relative to that root test dir.
 */
class SdkTestSyncNodeOperations: public SdkTestNodesSetUp
{
public:
    void SetUp() override
    {
        SdkTestNodesSetUp::SetUp();
        ASSERT_NO_FATAL_FAILURE(initiateSync());
    }

    void TearDown() override
    {
        ASSERT_NO_FATAL_FAILURE(removeSync());
        SdkTestNodesSetUp::TearDown();
    }

    /**
     * @brief Build a simple file tree
     */
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> ELEMENTS{
            DirNodeInfo("dir1").addChild(FileNodeInfo("testFile")),
            DirNodeInfo("dir2")};
        return ELEMENTS;
    }

    const std::string& getRootTestDir() const override
    {
        static const std::string dirName{"SDK_TEST_SYNC_NODE_OPERATIONS_AUX_DIR"};
        return dirName;
    }

    /**
     * @brief We don't want different creation times
     */
    bool keepDifferentCreationTimes() override
    {
        return false;
    }

    /**
     * @brief Where should we put our sync locally?
     */
    static const fs::path& getLocalTmpDir()
    {
        // Prevent parallel test from the same suite writing to the same dir
        thread_local const fs::path localTmpDir{"./SDK_TEST_SYNC_NODE_OPERATIONS_AUX_LOCAL_DIR_" +
                                                getThisThreadIdStr()};
        return localTmpDir;
    }

    /**
     * @brief Returns the identifier to get the sync from the megaApi
     */
    handle getBackupId() const
    {
        return mBackupId;
    }

    /**
     * @brief Returns the current sync state
     */
    std::unique_ptr<MegaSync> getSync() const
    {
        return std::unique_ptr<MegaSync>(megaApi[0]->getSyncByBackupId(mBackupId));
    }

    /**
     * @brief Moves the cloud node that is in the relative path "sourcePath" to the relative
     * "destPath"
     */
    void moveRemoteNode(const std::string& sourcePath, const std::string& destPath)
    {
        const auto source = getNodeByPath(sourcePath);
        const auto dest = getNodeByPath(destPath);
        ASSERT_EQ(API_OK, doMoveNode(0, nullptr, source.get(), dest.get()));
    }

    /**
     * @brief Renames the remote node located at sourcePath with the new given name
     */
    void renameRemoteNode(const std::string& sourcePath, const std::string& newName)
    {
        const auto source = getNodeByPath(sourcePath);
        ASSERT_EQ(API_OK, doRenameNode(0, source.get(), newName.c_str()));
    }

    /**
     * @brief Removes the node located at the give relative path
     */
    void removeRemoteNode(const std::string& path)
    {
        const auto node = getNodeByPath(path);
        ASSERT_EQ(API_OK, doDeleteNode(0, node.get()));
    }

    /**
     * @brief Asserts there is a sync pointing to the remote relative path and that it is in
     * RUNSTATE_RUNNING
     */
    void ensureSyncNodeIsRunning(const std::string& path)
    {
        const auto syncNode = getNodeByPath(path);
        ASSERT_TRUE(syncNode);
        const auto sync = megaApi[0]->getSyncByNode(syncNode.get());
        ASSERT_TRUE(sync);
        ASSERT_EQ(sync->getRunState(), MegaSync::RUNSTATE_RUNNING);
    }

    /**
     * @brief Asserts that the sync last known remote folder matches with the one give relative path
     */
    void ensureSyncLastKnownMegaFolder(const std::string& path)
    {
        std::unique_ptr<MegaSync> sync(megaApi[0]->getSyncByBackupId(getBackupId()));
        ASSERT_TRUE(sync);
        ASSERT_EQ(sync->getLastKnownMegaFolder(), convertToTestPath(path));
    }

private:
    LocalTempDir mTempLocalDir{getLocalTmpDir()};
    handle mBackupId{UNDEF};

    void initiateSync()
    {
        LOG_verbose << "SdkTestSyncNodeOperations : Initiate sync";
        auto lp = getLocalTmpDir().u8string();
        const auto syncNode = getNodeByPath("dir1/");
        ASSERT_EQ(API_OK,
                  synchronousSyncFolder(0,
                                        nullptr,
                                        MegaSync::TYPE_TWOWAY,
                                        lp.c_str(),
                                        nullptr,
                                        syncNode->getHandle(),
                                        nullptr))
            << "API Error adding a new sync";
        ASSERT_EQ(MegaSync::NO_SYNC_ERROR, mApi[0].lastSyncError);
        std::unique_ptr<MegaSync> sync = sdk_test::waitForSyncState(megaApi[0].get(),
                                                                    syncNode.get(),
                                                                    MegaSync::RUNSTATE_RUNNING,
                                                                    MegaSync::NO_SYNC_ERROR);
        ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_RUNNING);
        ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());
        mBackupId = sync->getBackupId();
    }

    void removeSync()
    {
        LOG_verbose << "SdkTestSyncNodeOperations : Remove sync";
        const auto rt = std::make_unique<RequestTracker>(megaApi[0].get());
        megaApi[0]->removeSync(mBackupId, rt.get());
        ASSERT_EQ(rt->waitForResult(), API_OK);
    }
};

TEST_F(SdkTestSyncNodeOperations, MoveRemoteRoot)
{
    static constexpr std::string_view logPre{"SdkTestSyncNodeOperations.MoveRemoteRoot : "};

    // The state of the sync shouldn't change so we will be checking that all across the test
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));
    ASSERT_NO_FATAL_FAILURE(ensureSyncLastKnownMegaFolder("dir1"));

    LOG_verbose << logPre << "Rename remote root from dir1 to dir1moved";
    ASSERT_NO_FATAL_FAILURE(renameRemoteNode("dir1", "dir1moved"));
    std::this_thread::sleep_for(3s);

    // Now the sync should be running on the moved dir
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1moved"));
    ASSERT_NO_FATAL_FAILURE(ensureSyncLastKnownMegaFolder("dir1moved"));

    LOG_verbose << logPre << "Move the remote root (put dir1moved inside dir2)";
    ASSERT_NO_FATAL_FAILURE(moveRemoteNode("dir1moved", "dir2/"));
    std::this_thread::sleep_for(3s);

    // Now the sync should be running on the moved dir
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir2/dir1moved"));
    ASSERT_NO_FATAL_FAILURE(ensureSyncLastKnownMegaFolder("dir2/dir1moved"));
}

TEST_F(SdkTestSyncNodeOperations, RemoveRemoteRoot)
{
    static constexpr std::string_view logPre{"SdkTestSyncNodeOperations.RemoveRemoteRoot : "};

    // We expect the sync to stop if the remote root node gets deleted
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));

    LOG_verbose << logPre << "Remove remote root (dir1)";
    ASSERT_NO_FATAL_FAILURE(removeRemoteNode("dir1"));

    const auto sync = waitForSyncState(megaApi[0].get(),
                                       getBackupId(),
                                       MegaSync::RUNSTATE_SUSPENDED,
                                       MegaSync::REMOTE_NODE_NOT_FOUND);
    ASSERT_TRUE(sync);
    ASSERT_EQ(sync->getRunState(), MegaSync::RUNSTATE_SUSPENDED);
    ASSERT_EQ(sync->getError(), MegaSync::REMOTE_NODE_NOT_FOUND);
}
