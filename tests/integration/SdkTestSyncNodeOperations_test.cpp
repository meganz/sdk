/**
 * @file
 * @brief This file is expected to contain tests involving syncs and operations with nodes (local
 * and remote), e.g., what happens when the remote root of a sync gets deleted.
 */

#ifdef ENABLE_SYNC

#include "integration_test_utils.h"
#include "megautils.h"
#include "mock_listeners.h"
#include "sdk_test_utils.h"
#include "SdkTestNodesSetUp_test.h"

#include <gmock/gmock.h>

using namespace sdk_test;
using namespace testing;

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
        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());
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

    /**
     * @brief Waits until all direct successors from both remote and local roots of the sync matches
     *
     * Asserts false if a timeout is overpassed.
     */
    void waitForSyncToMatchCloudAndLocal()
    {
        const auto areLocalAndCloudSynched = [this]() -> bool
        {
            const auto childrenLocalName = getLocalFirstChildrenNames();
            const auto childrenCloudName = getCloudFirstChildrenNames();
            return childrenCloudName && childrenCloudName->size() == childrenLocalName.size() &&
                   std::all_of(begin(childrenLocalName),
                               end(childrenLocalName),
                               [remoteRoot = std::string{getSync()->getLastKnownMegaFolder()},
                                this](const std::string& childName) -> bool
                               {
                                   return megaApi[0]->getNodeByPath(
                                              (remoteRoot + "/" + childName).c_str()) != nullptr;
                               });
        };
        ASSERT_TRUE(waitFor(areLocalAndCloudSynched, 3min, 10s));
    }

    /**
     * @brief Returns a vector with the names of the first successor files/directories inside the
     * local root.
     *
     * Hidden files (starting with . are excludoed)
     */
    std::vector<std::string> getLocalFirstChildrenNames() const
    {
        std::vector<std::string> result;
        const auto pushName = [&result](const std::filesystem::path& path)
        {
            if (const auto name = path.filename().string();
                name.front() != '.' && name != DEBRISFOLDER)
                result.emplace_back(std::move(name));
        };
        std::filesystem::directory_iterator children{getLocalTmpDir()};
        std::for_each(begin(children), end(children), pushName);
        return result;
    }

    /**
     * @brief Returns a vector with the names of the first successor nodes inside the cloud root
     * node. nullopt is returned in case the remote root node does not exist.
     */
    std::optional<std::vector<std::string>> getCloudFirstChildrenNames() const
    {
        const auto nodeHandle = getSync()->getMegaHandle();
        if (nodeHandle == UNDEF)
            return {};
        std::unique_ptr<MegaNode> rootNode{megaApi[0]->getNodeByHandle(nodeHandle)};
        if (!rootNode)
            return {};
        std::unique_ptr<MegaNodeList> childrenNodeList{megaApi[0]->getChildren(rootNode.get())};
        if (!childrenNodeList)
            return {};
        return toNamesVector(*childrenNodeList);
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

    void changeRemoteRootNodeAndWaitForSyncUpdate(const std::string& destRemotePath)
    {
        const auto newRootHandleOpt = getNodeHandleByPath(destRemotePath);
        ASSERT_TRUE(newRootHandleOpt);

        // Expectations on a global listener for the onSyncRemoteRootChanged
        const auto HasGoodName = Pointee(
            Property(&MegaSync::getLastKnownMegaFolder, StrEq(convertToTestPath(destRemotePath))));
        const auto HasGoodRunState =
            Pointee(Property(&MegaSync::getRunState, MegaSync::RUNSTATE_RUNNING));

        NiceMock<MockSyncListener> mockSyncListener;
        // Expect call to onSyncRemoteRootChanged on the global listener
        std::promise<void> remoteRootChangedFinished;
        EXPECT_CALL(mockSyncListener,
                    onSyncRemoteRootChanged(_, AllOf(HasGoodName, HasGoodRunState)))
            .WillOnce(
                [&remoteRootChangedFinished]
                {
                    remoteRootChangedFinished.set_value();
                });

        // Expectations on the request listener
        NiceMock<MockRequestListener> mockReqListener;
        mockReqListener.setErrorExpectations(API_OK, _, MegaRequest::TYPE_CHANGE_SYNC_ROOT);

        // Code execution
        megaApi[0]->addListener(&mockSyncListener);
        megaApi[0]->changeSyncRemoteRoot(getBackupId(), *newRootHandleOpt, &mockReqListener);

        // Wait for everything to finish
        mockReqListener.waitForFinishOrTimeout(3min);
        EXPECT_EQ(remoteRootChangedFinished.get_future().wait_for(3min), future_status::ready);

        // Remove the listener
        megaApi[0]->removeListener(&mockSyncListener);
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

TEST_F(SdkTestSyncNodeOperations, ChangeSyncRemoteRootErrors)
{
    static const std::string logPre{"SdkTestSyncNodeOperations.ChangeSyncRemoteRootErrors : "};

    {
        LOG_verbose << logPre << "Giving undef backupId and undef remote handle";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, _);
        megaApi[0]->changeSyncRemoteRoot(UNDEF, UNDEF, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(3min));
    }

    const auto newRootHandleOpt{getNodeHandleByPath("dir2")};
    ASSERT_TRUE(newRootHandleOpt);
    const MegaHandle newRootHandle{*newRootHandleOpt};

    {
        LOG_verbose << logPre << "Giving undef backupId and good remote handle";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, _);
        megaApi[0]->changeSyncRemoteRoot(UNDEF, newRootHandle, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(3min));
    }

    {
        LOG_verbose << logPre << "Giving non existent backupId and good remote handle";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, UNKNOWN_ERROR);
        megaApi[0]->changeSyncRemoteRoot(newRootHandle, newRootHandle, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(3min));
    }

    {
        LOG_verbose << logPre << "Giving good backupId and a handle to a file node";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EACCESS, INVALID_REMOTE_TYPE);
        const auto fileHandle = getNodeHandleByPath("dir1/testFile");
        ASSERT_TRUE(fileHandle);
        megaApi[0]->changeSyncRemoteRoot(getBackupId(), *fileHandle, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(3min));
    }

    {
        LOG_verbose << logPre << "Giving good backupId and handle to already synced root";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EEXIST, ACTIVE_SYNC_SAME_PATH);
        const auto dir1Handle = getNodeHandleByPath("dir1");
        ASSERT_TRUE(dir1Handle);
        megaApi[0]->changeSyncRemoteRoot(getBackupId(), *dir1Handle, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(3min));
    }

    // Just make sure that after all the attempts the sync is still running fine
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));
}

TEST_F(SdkTestSyncNodeOperations, ChangeSyncRemoteRootOK)
{
    static const std::string logPre{"SdkTestSyncNodeOperations.ChangeSyncRemoteRootOK : "};

    // Initial state: dir1 contains testFile
    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));
    LOG_verbose << logPre << "Changing sync remote root to point dir2";
    ASSERT_NO_FATAL_FAILURE(changeRemoteRootNodeAndWaitForSyncUpdate("dir2"));
    // Note: dir2 is empty
    LOG_verbose << logPre << "Ensuring sync is running on dir2";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir2"));
    LOG_verbose << logPre << "Waiting for sync remote and local roots to have the same content";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    const std::vector<std::string> expectations{};
    EXPECT_THAT(expectations, testing::UnorderedElementsAreArray(getLocalFirstChildrenNames()));
}
#endif // ENABLE_SYNC
