/**
 * @file
 * @brief This file is expected to contain tests involving sync root paths (local
 * and remote), e.g., what happens when the remote root of a sync gets deleted.
 */

#ifdef ENABLE_SYNC

#include "integration_test_utils.h"
#include "mega/utils.h"
#include "megautils.h"
#include "mock_listeners.h"
#include "sdk_test_utils.h"
#include "SdkTestSyncNodesOperations_test.h"

#include <gmock/gmock.h>

using namespace sdk_test;
using namespace testing;

/**
 * @class SdkTestSyncRootOperations
 * @brief Test fixture designed to test operations involving sync root local and remote paths.
 */
class SdkTestSyncRootOperations: public SdkTestSyncNodesOperations
{
public:
    static constexpr auto MAX_TIMEOUT =
        COMMON_TIMEOUT; // Timeout for operations in this tests suite

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
        NiceMock<MockRequestListener> mockReqListener{megaApi[0].get()};
        mockReqListener.setErrorExpectations(API_OK, _, MegaRequest::TYPE_CHANGE_SYNC_ROOT);

        // Code execution
        megaApi[0]->changeSyncRemoteRoot(getBackupId(), *newRootHandleOpt, &mockReqListener);

        // Wait for everything to finish
        mockReqListener.waitForFinishOrTimeout(MAX_TIMEOUT);
    }
};

TEST_F(SdkTestSyncRootOperations, MoveRemoteRoot)
{
    static const std::string logPre{"SdkTestSyncRootOperations.MoveRemoteRoot : "};

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

TEST_F(SdkTestSyncRootOperations, RemoveRemoteRoot)
{
    static const std::string logPre{"SdkTestSyncRootOperations.RemoveRemoteRoot : "};

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

TEST_F(SdkTestSyncRootOperations, MoveSyncToAnotherSync)
{
    static const std::string logPre{"SdkTestSyncRootOperations.MoveSyncToAnotherSync : "};

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
 * @brief SdkTestSyncRootOperations.ChangeSyncRemoteRootErrors
 *
 * Tests multiple error paths when calling changeSyncRemoteRoot
 */
TEST_F(SdkTestSyncRootOperations, ChangeSyncRemoteRootErrors)
{
    static const std::string logPre{"SdkTestSyncRootOperations.ChangeSyncRemoteRootErrors : "};

    {
        LOG_verbose << logPre << "Giving undef backupId and undef remote handle";
        NiceMock<MockRequestListener> mockListener{megaApi[0].get()};
        mockListener.setErrorExpectations(API_EARGS, _);
        megaApi[0]->changeSyncRemoteRoot(UNDEF, UNDEF, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    const auto newRootHandleOpt{getNodeHandleByPath("dir2")};
    ASSERT_TRUE(newRootHandleOpt);
    const MegaHandle newRootHandle{*newRootHandleOpt};

    {
        LOG_verbose << logPre << "Giving undef backupId and good remote handle";
        NiceMock<MockRequestListener> mockListener{megaApi[0].get()};
        mockListener.setErrorExpectations(API_EARGS, _);
        megaApi[0]->changeSyncRemoteRoot(UNDEF, newRootHandle, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_verbose << logPre << "Giving non existent backupId and good remote handle";
        NiceMock<MockRequestListener> mockListener{megaApi[0].get()};
        mockListener.setErrorExpectations(API_EARGS, UNKNOWN_ERROR);
        megaApi[0]->changeSyncRemoteRoot(newRootHandle, newRootHandle, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_verbose << logPre << "Giving good backupId and a handle to a file node";
        NiceMock<MockRequestListener> mockListener{megaApi[0].get()};
        mockListener.setErrorExpectations(API_EACCESS, INVALID_REMOTE_TYPE);
        const auto fileHandle = getNodeHandleByPath("dir1/testFile");
        ASSERT_TRUE(fileHandle);
        megaApi[0]->changeSyncRemoteRoot(getBackupId(), *fileHandle, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_verbose << logPre << "Giving good backupId and handle to already synced root";
        NiceMock<MockRequestListener> mockListener{megaApi[0].get()};
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
 * @brief SdkTestSyncRootOperations.ChangeSyncRemoteRootErrorOnBackup
 *
 * Checks that changing the remote root of a backup returns an error (not allowed operation).
 */
TEST_F(SdkTestSyncRootOperations, ChangeSyncRemoteRootErrorOnBackup)
{
    static const std::string logPre{
        "SdkTestSyncRootOperations.ChangeSyncRemoteRootErrorOnBackup : "};

    LOG_verbose << logPre << "Create a backup";
    ASSERT_NO_FATAL_FAILURE(ensureAccountDeviceName(megaApi[0].get()));
    LocalTempDir tmpDir{"auxChangeSyncRemoteRootErrorOnBackupDir"};

    const auto backupId = backupFolder(megaApi[0].get(), tmpDir.getPath().u8string());
    ASSERT_NE(backupId, UNDEF) << "Error initiating the backup";
    const MrProper defer{[backupId, &api = megaApi[0]]()
                         {
                             removeSync(api.get(), backupId);
                         }};

    LOG_verbose << logPre << "Wait for the backup to enter in RUNNING state";
    const auto backup = waitForSyncState(megaApi[0].get(),
                                         backupId,
                                         MegaSync::RUNSTATE_RUNNING,
                                         MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(backup) << "Unable to get the backup in RUNNING state";

    LOG_verbose << logPre << "Trying to change the remote root of a backup sync";
    NiceMock<MockRequestListener> mockListener{megaApi[0].get()};
    mockListener.setErrorExpectations(API_EARGS, UNKNOWN_ERROR);
    const auto dir1Handle = getNodeHandleByPath("dir2");
    ASSERT_TRUE(dir1Handle);
    megaApi[0]->changeSyncRemoteRoot(backupId, *dir1Handle, &mockListener);
    EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
}

/**
 * @brief SdkTestSyncRootOperations.ChangeSyncRemoteRootOK
 *
 * Changes the remote root node of the running sync and validates the final state (which is expected
 * to mimic the state of the new root)
 */
TEST_F(SdkTestSyncRootOperations, ChangeSyncRemoteRootOK)
{
    static const std::string logPre{"SdkTestSyncRootOperations.ChangeSyncRemoteRootOK : "};

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
 * @brief SdkTestSyncRootOperations.ChangeSyncRemoteRootWhenSyncPausedOK
 *
 * Same as SdkTestSyncRootOperations.ChangeSyncRemoteRootOK but the change is applied on a paused
 * sync. Once the change is done, the sync gets resumed and the final state is validated.
 */
TEST_F(SdkTestSyncRootOperations, ChangeSyncRemoteRootWhenSyncPausedOK)
{
    static const std::string logPre{
        "SdkTestSyncRootOperations.ChangeSyncRemoteRootWhenSyncPausedOK : "};

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
 * @brief SdkTestSyncRootOperations.ChangeSyncRemoteRootWhenSyncDisableOK
 *
 * Changes the remote root node of a sync that has been disabled. Then it is resumed and the final
 * state is validated.
 *
 * @note In this case, as the local nodes database is removed after disabling, a mirroring is
 * expected after resuming.
 */
TEST_F(SdkTestSyncRootOperations, ChangeSyncRemoteRootWhenSyncDisableOK)
{
    static const std::string logPre{
        "SdkTestSyncRootOperations.ChangeSyncRemoteRootWhenSyncDisableOK : "};

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
 * @brief SdkTestSyncRootOperations.ChangeSyncRemoteRootPersistsAfterDisable
 *
 * Changes the remote root node of the running sync, suspends it, resumes it and validates the final
 * state (which is expected to mimic the state of the new root)
 */
TEST_F(SdkTestSyncRootOperations, ChangeSyncRemoteRootPersistsAfterDisabled)
{
    static const std::string logPre{"SdkTestSyncRootOperations.ChangeSyncRemoteRootOK : "};

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
 * @brief SdkTestSyncRootOperations.ChangeSyncRemoteRootWhenTransfersInProgress
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
TEST_F(SdkTestSyncRootOperations, ChangeSyncRemoteRootWhenTransfersInProgress)
{
    static const std::string logPre{
        "SdkTestSyncRootOperations.ChangeSyncRemoteRootWhenTransfersInProgress : "};

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
