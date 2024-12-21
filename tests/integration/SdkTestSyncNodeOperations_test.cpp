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
    static constexpr auto MAX_TIMEOUT = 3min; // Timeout for operations in this tests suite

    void SetUp() override
    {
        SdkTestNodesSetUp::SetUp();
        ASSERT_NO_FATAL_FAILURE(initiateSync(getLocalTmpDir().u8string(), "dir1/", mBackupId));
        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());
    }

    void TearDown() override
    {
        ASSERT_TRUE(removeSync(megaApi[0].get(), mBackupId));
        SdkTestNodesSetUp::TearDown();
    }

    /**
     * @brief Build a simple file tree
     */
    const std::vector<NodeInfo>& getElements() const override
    {
        // To ensure "testCommonFile" is identical in both dirs
        static const auto currentTime = std::chrono::system_clock::now();
        static const std::vector<NodeInfo> ELEMENTS{
            DirNodeInfo("dir1")
                .addChild(FileNodeInfo("testFile").setSize(1))
                .addChild(FileNodeInfo("testCommonFile").setMtime(currentTime))
                .addChild(FileNodeInfo("testFile1")),
            DirNodeInfo("dir2")
                .addChild(FileNodeInfo("testFile").setSize(2))
                .addChild(FileNodeInfo("testCommonFile").setMtime(currentTime))
                .addChild(FileNodeInfo("testFile2"))};
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

    void suspendSync()
    {
        ASSERT_TRUE(sdk_test::suspendSync(megaApi[0].get(), mBackupId))
            << "Error when trying to suspend the sync";
    }

    void disableSync()
    {
        ASSERT_TRUE(sdk_test::disableSync(megaApi[0].get(), mBackupId))
            << "Error when trying to disable the sync";
    }

    void resumeSync()
    {
        ASSERT_TRUE(sdk_test::resumeSync(megaApi[0].get(), mBackupId))
            << "Error when trying to resume the sync";
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
        backupId = sdk_test::syncFolder(megaApi[0].get(),
                                        localPath,
                                        getNodeByPath(remotePath)->getHandle());
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
            const auto childrenCloudName =
                getCloudFirstChildrenNames(megaApi[0].get(), getSync()->getMegaHandle());
            return childrenCloudName && Value(getLocalFirstChildrenNames(),
                                              UnorderedElementsAreArray(*childrenCloudName));
        };
        ASSERT_TRUE(waitFor(areLocalAndCloudSynched, MAX_TIMEOUT, 10s));
    }

    void checkCurrentLocalMatchesOriginal(const std::string_view cloudDirName)
    {
        const auto& originals = getElements();
        const auto it = std::find_if(std::begin(originals),
                                     std::end(originals),
                                     [&cloudDirName](const auto& node)
                                     {
                                         return getNodeName(node) == cloudDirName;
                                     });
        ASSERT_NE(it, std::end(originals))
            << cloudDirName << ": directory not found in original elements";
        const auto* dirNode = std::get_if<DirNodeInfo>(&(*it));
        ASSERT_TRUE(dirNode) << "The found original element is not a directory";

        using ChildNameSize = std::pair<std::string, std::optional<unsigned>>;
        // Get info from original cloud
        std::vector<ChildNameSize> childOriginalInfo;
        std::transform(std::begin(dirNode->childs),
                       std::end(dirNode->childs),
                       std::back_inserter(childOriginalInfo),
                       [](const auto& child) -> ChildNameSize
                       {
                           return std::visit(
                               overloaded{[](const DirNodeInfo& dir) -> ChildNameSize
                                          {
                                              return {dir.name, {}};
                                          },
                                          [](const FileNodeInfo& file) -> ChildNameSize
                                          {
                                              return {file.name, file.size};
                                          }},
                               child);
                       });

        // Get info from current local
        std::vector<ChildNameSize> childLocalInfo;
        std::filesystem::directory_iterator children{getLocalTmpDir()};
        std::for_each(begin(children),
                      end(children),
                      [&childLocalInfo](const std::filesystem::path& path)
                      {
                          const auto name = path.filename().string();
                          if (name.front() == '.' || name == DEBRISFOLDER)
                              return;
                          if (std::filesystem::is_directory(path))
                              childLocalInfo.push_back({name, {}});
                          else
                              childLocalInfo.push_back(
                                  {name, static_cast<unsigned>(std::filesystem::file_size(path))});
                      });

        ASSERT_THAT(childLocalInfo, testing::UnorderedElementsAreArray(childOriginalInfo));
    }

    /**
     * @brief Asserts that there are 2 stall issues pointing to local paths that end with the given
     * names and their reason is LocalAndRemotePreviouslyUnsyncedDiffer_userMustChoose.
     *
     * Useful to validate mirroring state between dir1 and dir2.
     */
    void thereIsAStall(const std::string_view fileName) const
    {
        const auto stalls = sdk_test::getStalls(megaApi[0].get());
        ASSERT_EQ(stalls.size(), 1);
        ASSERT_TRUE(stalls[0]);
        const auto& stall = *stalls[0];
        ASSERT_THAT(stall.path(false, 0), EndsWith(fileName));
        ASSERT_THAT(
            stall.reason(),
            MegaSyncStall::SyncStallReason::LocalAndRemotePreviouslyUnsyncedDiffer_userMustChoose);
    }

    /**
     * @brief Asserts that the local sync directory contains all the files matching a mirroring
     * state (all the files in dir1 merged with those in dir2)
     */
    void checkCurrentLocalMatchesMirror() const
    {
        ASSERT_THAT(getLocalFirstChildrenNames(),
                    UnorderedElementsAre("testFile", "testCommonFile", "testFile1", "testFile2"));
        ASSERT_NO_FATAL_FAILURE(thereIsAStall("testFile"));
    }

    /**
     * @brief Returns a vector with the names of the first successor files/directories inside the
     * local root.
     *
     * Hidden files (starting with . are excludoed)
     */
    std::vector<std::string> getLocalFirstChildrenNames() const
    {
        return sdk_test::getLocalFirstChildrenNames_if(getLocalTmpDir(),
                                                       [](const std::string& name)
                                                       {
                                                           return name.front() != '.' &&
                                                                  name != DEBRISFOLDER;
                                                       });
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
        MrProper clean{[&api = megaApi[0], &ml]()
                       {
                           api->removeListener(&ml);
                       }};
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
        ASSERT_EQ(renameFinished.get_future().wait_for(MAX_TIMEOUT), std::future_status::ready)
            << "The sync root movement didn't take place within 3 mins";
    }

    void changeRemoteRootNodeAndWaitForSyncUpdate(const std::string& destRemotePath)
    {
        const auto newRootHandleOpt = getNodeHandleByPath(destRemotePath);
        ASSERT_TRUE(newRootHandleOpt);

        // Expectations on the request listener
        NiceMock<MockRequestListener> mockReqListener;
        mockReqListener.setErrorExpectations(API_OK, _, MegaRequest::TYPE_CHANGE_SYNC_ROOT);

        // Code execution
        megaApi[0]->changeSyncRemoteRoot(getBackupId(), *newRootHandleOpt, &mockReqListener);

        // Wait for everything to finish
        mockReqListener.waitForFinishOrTimeout(MAX_TIMEOUT);
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
        [&dir2SyncId, &api = megaApi[0]]()
        {
            ASSERT_TRUE(removeSync(api.get(), dir2SyncId));
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

/**
 * @brief SdkTestSyncNodeOperations.ChangeSyncRemoteRootErrors
 *
 * Tests multiple error paths when calling changeSyncRemoteRoot
 */
TEST_F(SdkTestSyncNodeOperations, ChangeSyncRemoteRootErrors)
{
    static const std::string logPre{"SdkTestSyncNodeOperations.ChangeSyncRemoteRootErrors : "};

    {
        LOG_verbose << logPre << "Giving undef backupId and undef remote handle";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, _);
        megaApi[0]->changeSyncRemoteRoot(UNDEF, UNDEF, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    const auto newRootHandleOpt{getNodeHandleByPath("dir2")};
    ASSERT_TRUE(newRootHandleOpt);
    const MegaHandle newRootHandle{*newRootHandleOpt};

    {
        LOG_verbose << logPre << "Giving undef backupId and good remote handle";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, _);
        megaApi[0]->changeSyncRemoteRoot(UNDEF, newRootHandle, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_verbose << logPre << "Giving non existent backupId and good remote handle";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, UNKNOWN_ERROR);
        megaApi[0]->changeSyncRemoteRoot(newRootHandle, newRootHandle, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_verbose << logPre << "Giving good backupId and a handle to a file node";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EACCESS, INVALID_REMOTE_TYPE);
        const auto fileHandle = getNodeHandleByPath("dir1/testFile");
        ASSERT_TRUE(fileHandle);
        megaApi[0]->changeSyncRemoteRoot(getBackupId(), *fileHandle, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_verbose << logPre << "Giving good backupId and handle to already synced root";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EEXIST, ACTIVE_SYNC_SAME_PATH);
        const auto dir1Handle = getNodeHandleByPath("dir1");
        ASSERT_TRUE(dir1Handle);
        megaApi[0]->changeSyncRemoteRoot(getBackupId(), *dir1Handle, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    // Just make sure that after all the attempts the sync is still running fine
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));
}

/**
 * @brief SdkTestSyncNodeOperations.ChangeSyncRemoteRootOK
 *
 * Changes the remote root node of the running sync and validates the final state (which is expected
 * to mimic the state of the new root)
 */
TEST_F(SdkTestSyncNodeOperations, ChangeSyncRemoteRootOK)
{
    static const std::string logPre{"SdkTestSyncNodeOperations.ChangeSyncRemoteRootOK : "};

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));

    LOG_verbose << logPre << "Changing sync remote root to point dir2";
    ASSERT_NO_FATAL_FAILURE(changeRemoteRootNodeAndWaitForSyncUpdate("dir2"));

    LOG_verbose << logPre << "Ensuring sync is running on dir2";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir2"));

    LOG_verbose << logPre << "Waiting for sync remote and local roots to have the same content";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    LOG_verbose << logPre << "Check if the contents match expectations";
    ASSERT_NO_FATAL_FAILURE(checkCurrentLocalMatchesOriginal("dir2"));
}

/**
 * @brief SdkTestSyncNodeOperations.ChangeSyncRemoteRootWhenSyncPausedOK
 *
 * Same as SdkTestSyncNodeOperations.ChangeSyncRemoteRootOK but the change is applied on a paused
 * sync. Once the change is done, the sync gets resumed and the final state is validated.
 */
TEST_F(SdkTestSyncNodeOperations, ChangeSyncRemoteRootWhenSyncPausedOK)
{
    static const std::string logPre{
        "SdkTestSyncNodeOperations.ChangeSyncRemoteRootWhenSyncPausedOK : "};

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));

    LOG_verbose << logPre << "Suspending the sync";
    ASSERT_NO_FATAL_FAILURE(suspendSync());

    LOG_verbose << logPre << "Changing sync remote root to point dir2";
    ASSERT_NO_FATAL_FAILURE(changeRemoteRootNodeAndWaitForSyncUpdate("dir2"));

    LOG_verbose << logPre << "Resuming the sync";
    ASSERT_NO_FATAL_FAILURE(resumeSync());

    LOG_verbose << logPre << "Ensuring sync is running on dir2";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir2"));

    LOG_verbose << logPre << "Waiting for sync remote and local roots to have the same content";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    LOG_verbose << logPre << "Checking the final state";
    ASSERT_NO_FATAL_FAILURE(checkCurrentLocalMatchesOriginal("dir2"));
}

/**
 * @brief SdkTestSyncNodeOperations.ChangeSyncRemoteRootWhenSyncDisableOK
 *
 * Changes the remote root node of a sync that has been disabled. Then it is resumed and the final
 * state is validated.
 *
 * @note In this case, as the local nodes database is removed after disabling, a mirroring is
 * expected after resuming.
 */
TEST_F(SdkTestSyncNodeOperations, ChangeSyncRemoteRootWhenSyncDisableOK)
{
    static const std::string logPre{
        "SdkTestSyncNodeOperations.ChangeSyncRemoteRootWhenSyncDisableOK : "};

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));

    LOG_verbose << logPre << "Disabling the sync";
    ASSERT_NO_FATAL_FAILURE(disableSync());

    LOG_verbose << logPre << "Changing sync remote root to point dir2";
    ASSERT_NO_FATAL_FAILURE(changeRemoteRootNodeAndWaitForSyncUpdate("dir2"));

    LOG_verbose << logPre << "Resuming the sync";
    ASSERT_NO_FATAL_FAILURE(resumeSync());

    LOG_verbose << logPre << "Ensuring sync is running on dir2";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir2"));

    LOG_verbose << logPre << "Waiting for sync remote and local roots to have the same content";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    LOG_verbose << logPre << "Checking the final state";
    ASSERT_NO_FATAL_FAILURE(checkCurrentLocalMatchesMirror());
}

/**
 * @brief SdkTestSyncNodeOperations.ChangeSyncRemoteRootPersistsAfterDisable
 *
 * Changes the remote root node of the running sync, suspends it, resumes it and validates the final
 * state (which is expected to mimic the state of the new root)
 */
TEST_F(SdkTestSyncNodeOperations, ChangeSyncRemoteRootPersistsAfterDisabled)
{
    static const std::string logPre{"SdkTestSyncNodeOperations.ChangeSyncRemoteRootOK : "};

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));

    LOG_verbose << logPre << "Changing sync remote root to point dir2";
    ASSERT_NO_FATAL_FAILURE(changeRemoteRootNodeAndWaitForSyncUpdate("dir2"));

    LOG_verbose << logPre << "Suspending the sync";
    ASSERT_NO_FATAL_FAILURE(suspendSync());

    LOG_verbose << logPre << "Resuming the sync";
    ASSERT_NO_FATAL_FAILURE(resumeSync());

    LOG_verbose << logPre << "Ensuring sync is running on dir2";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir2"));

    LOG_verbose << logPre << "Waiting for sync remote and local roots to have the same content";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    LOG_verbose << logPre << "Check if the contents match expectations";
    ASSERT_NO_FATAL_FAILURE(checkCurrentLocalMatchesOriginal("dir2"));
}

/**
 * @brief SdkTestSyncNodeOperations.ChangeSyncRemoteRootWhenTransfersInProgress
 *
 * Similar to ChangeSyncRemoteRootOK but we must detect a transfer being cancelled and the file that
 * was being transferred will be removed as it is not in the new cloud root.
 *
 * 1. We create a file locally
 * 2. Wait until the transfer starts
 * 3. Call the changeSyncRemoteRoot method
 * 4. Expect the transfer to terminate
 * 5. Validate final state with the new root
 */
TEST_F(SdkTestSyncNodeOperations, ChangeSyncRemoteRootWhenTransfersInProgress)
{
    static const std::string logPre{
        "SdkTestSyncNodeOperations.ChangeSyncRemoteRootWhenTransfersInProgress : "};

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));

    LOG_verbose << logPre << "Setting up the mock listener";
    const auto dir1HandleOpt = getNodeHandleByPath("dir1");
    ASSERT_TRUE(dir1HandleOpt);
    const std::string_view newFileName{"test_file_new.txt"};

    const auto isMyFile = Pointee(Property(&MegaTransfer::getPath, EndsWith(newFileName)));
    const auto isUpload = Pointee(Property(&MegaTransfer::getType, MegaTransfer::TYPE_UPLOAD));
    const auto isBelowDir1 = Pointee(Property(&MegaTransfer::getParentHandle, *dir1HandleOpt));
    const auto isExpectedError = Pointee(Property(&MegaError::getErrorCode, API_EINCOMPLETE));

    NiceMock<MockTransferListener> mockListener{};
    std::promise<void> fileStartedUpload;
    EXPECT_CALL(mockListener, onTransferStart).Times(AnyNumber());
    EXPECT_CALL(mockListener, onTransferStart(_, AllOf(isMyFile, isUpload, isBelowDir1)))
        .WillOnce(
            [&fileStartedUpload]
            {
                fileStartedUpload.set_value();
            });
    std::promise<void> transferTerminated;
    EXPECT_CALL(mockListener, onTransferFinish).Times(AnyNumber());
    EXPECT_CALL(mockListener,
                onTransferFinish(_, AllOf(isMyFile, isUpload, isBelowDir1), isExpectedError))
        .WillOnce(
            [&transferTerminated]
            {
                transferTerminated.set_value();
            });
    // Register the listener
    megaApi[0]->addListener(&mockListener);
    MrProper clean{[&api = megaApi[0], &mockListener]()
                   {
                       api->removeListener(&mockListener);
                   }};

    LOG_verbose << logPre << "Create the new file locally";
    const auto newFilePath = getLocalTmpDir() / newFileName;
    LocalTempFile tempFile{newFilePath, 1000};

    LOG_verbose << logPre << "Waiting until transfer starts";
    ASSERT_EQ(fileStartedUpload.get_future().wait_for(MAX_TIMEOUT), std::future_status::ready)
        << "The upload didn't start within 3 mins";

    LOG_verbose << logPre << "Changing sync remote root to point dir2";
    ASSERT_NO_FATAL_FAILURE(changeRemoteRootNodeAndWaitForSyncUpdate("dir2"));

    LOG_verbose << logPre << "Waiting transfer to be terminated with error";
    ASSERT_EQ(transferTerminated.get_future().wait_for(MAX_TIMEOUT), std::future_status::ready)
        << "The upload didn't start within 3 mins";
}
#endif // ENABLE_SYNC
