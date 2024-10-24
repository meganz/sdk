/**
 * @file
 * @brief This file is expected to contain tests involving syncs and operations with nodes (local
 * and remote), e.g., what happens when the remote root of a sync gets deleted.
 */

#include "integration_test_utils.h"
#include "SdkTestNodesSetUp_test.h"

#include <gmock/gmock.h>

using namespace sdk_test;
using namespace testing;

class MockSyncListener: public MegaListener
{
public:
    MOCK_METHOD(void,
                onSyncFileStateChanged,
                (MegaApi * api, MegaSync* sync, std::string* localPath, int newState),
                (override));
    MOCK_METHOD(void, onSyncAdded, (MegaApi * api, MegaSync* sync), (override));
    MOCK_METHOD(void, onSyncDeleted, (MegaApi * api, MegaSync* sync), (override));
    MOCK_METHOD(void, onSyncStateChanged, (MegaApi * api, MegaSync* sync), (override));
    MOCK_METHOD(void, onSyncStatsUpdated, (MegaApi * api, MegaSyncStats* syncStats), (override));
    MOCK_METHOD(void, onGlobalSyncStateChanged, (MegaApi * api), (override));
    MOCK_METHOD(void, onSyncRemoteRootChanged, (MegaApi * api, MegaSync* sync), (override));
    MOCK_METHOD(void, onRequestFinish, (MegaApi*, MegaRequest*, MegaError*), (override));
};

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
        ASSERT_NO_FATAL_FAILURE(initiateSync(getLocalTmpDir().u8string(), "dir1/", mBackupId));
    }

    void TearDown() override
    {
        ASSERT_NO_FATAL_FAILURE(removeSync(mBackupId));
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

    void initiateSync(const std::string& localPath,
                      const std::string& remotePath,
                      MegaHandle& backupId)
    {
        LOG_verbose << "SdkTestSyncNodeOperations : Initiate sync";
        const auto syncNode = getNodeByPath(remotePath);
        ASSERT_EQ(API_OK,
                  synchronousSyncFolder(0,
                                        nullptr,
                                        MegaSync::TYPE_TWOWAY,
                                        localPath.c_str(),
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
        backupId = sync->getBackupId();
    }

    void removeSync(const MegaHandle backupId)
    {
        LOG_verbose << "SdkTestSyncNodeOperations : Remove sync";
        const auto rt = std::make_unique<RequestTracker>(megaApi[0].get());
        megaApi[0]->removeSync(backupId, rt.get());
        ASSERT_EQ(rt->waitForResult(), API_OK);
    }

    enum class MoveOp
    {
        MOVE,
        RENAME
    };

    void moveRemoteRootAndWaitForSyncUpdate(const std::string& sourcePath,
                                            const std::string& destPath,
                                            const MoveOp rename = MoveOp::MOVE)
    {
        // Expectations
        std::string expectedNewRootPath = destPath;
        if (rename == MoveOp::MOVE && destPath.back() == '/')
            expectedNewRootPath += sourcePath;

        const auto hasGoodName = Pointee(Property(&MegaSync::getLastKnownMegaFolder,
                                                  StrEq(convertToTestPath(expectedNewRootPath))));
        const auto hasGoodRunState =
            Pointee(Property(&MegaSync::getRunState, MegaSync::RUNSTATE_RUNNING));

        std::promise<void> renameFinished;
        NiceMock<MockSyncListener> ml;
        EXPECT_CALL(ml, onSyncRemoteRootChanged(_, AllOf(hasGoodName, hasGoodRunState)))
            .WillOnce(
                [&renameFinished]
                {
                    renameFinished.set_value();
                });

        // Code execution
        megaApi[0]->addListener(&ml);
        switch (rename)
        {
            case MoveOp::MOVE:
                ASSERT_NO_FATAL_FAILURE(moveRemoteNode(sourcePath, destPath));
                break;
            case MoveOp::RENAME:
                ASSERT_NO_FATAL_FAILURE(renameRemoteNode(sourcePath, destPath));
                break;
        }
        // Wait for finish
        renameFinished.get_future().wait();
        megaApi[0]->removeListener(&ml);
    }

private:
    LocalTempDir mTempLocalDir{getLocalTmpDir()};
    handle mBackupId{UNDEF};
};

TEST_F(SdkTestSyncNodeOperations, MoveRemoteRoot)
{
    static const std::string logPre{"SdkTestSyncNodeOperations.MoveRemoteRoot : "};

    // The state of the sync shouldn't change so we will be checking that all across the test
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));
    ASSERT_NO_FATAL_FAILURE(ensureSyncLastKnownMegaFolder("dir1"));

    LOG_verbose << logPre << "Rename remote root from dir1 to dir1moved";
    ASSERT_NO_FATAL_FAILURE(
        moveRemoteRootAndWaitForSyncUpdate("dir1", "dir1moved", MoveOp::RENAME));

    // Now the sync should be running on the moved dir
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1moved"));
    ASSERT_NO_FATAL_FAILURE(ensureSyncLastKnownMegaFolder("dir1moved"));

    LOG_verbose << logPre << "Move the remote root (put dir1moved inside dir2)";
    ASSERT_NO_FATAL_FAILURE(moveRemoteRootAndWaitForSyncUpdate("dir1moved", "dir2/", MoveOp::MOVE));

    // Now the sync should be running on the moved dir
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir2/dir1moved"));
    ASSERT_NO_FATAL_FAILURE(ensureSyncLastKnownMegaFolder("dir2/dir1moved"));
}

TEST_F(SdkTestSyncNodeOperations, RemoveRemoteRoot)
{
    static const std::string logPre{"SdkTestSyncNodeOperations.RemoveRemoteRoot : "};

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

TEST_F(SdkTestSyncNodeOperations, MoveSyncToAnotherSync)
{
    static const std::string logPre{"SdkTestSyncNodeOperations.MoveSyncToAnotherSync : "};

    // Moving a sync to another sync should disable it
    LOG_verbose << logPre << "Create a new sync in dir2";
    std::string tempLocalDir2Name = getLocalTmpDir().u8string() + "2";
    LocalTempDir tempLocalDir2{tempLocalDir2Name};
    MegaHandle dir2SyncId;
    ASSERT_NO_FATAL_FAILURE(initiateSync(tempLocalDir2Name, "dir2/", dir2SyncId));
    // Make sure it is removed after exiting the scope
    const auto autoRemove = MrProper(
        [&dir2SyncId, this]()
        {
            ASSERT_NO_FATAL_FAILURE(removeSync(dir2SyncId));
        });

    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir2"));

    LOG_verbose << logPre << "Moving dir1 inside dir2";
    ASSERT_NO_FATAL_FAILURE(moveRemoteNode("dir1", "dir2/"));

    LOG_verbose << logPre << "Waiting for dir1 to be disable as it is inside another sync";
    const auto sync = waitForSyncState(megaApi[0].get(),
                                       getBackupId(),
                                       MegaSync::RUNSTATE_SUSPENDED,
                                       MegaSync::ACTIVE_SYNC_ABOVE_PATH);
    ASSERT_TRUE(sync);
    ASSERT_EQ(sync->getRunState(), MegaSync::RUNSTATE_SUSPENDED);
    ASSERT_EQ(sync->getError(), MegaSync::ACTIVE_SYNC_ABOVE_PATH);
}
