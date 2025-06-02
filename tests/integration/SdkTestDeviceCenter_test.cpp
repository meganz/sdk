/**
 * @file SdkTestDeviceCenter_test.cpp
 * @brief Test Device Center operations on full-account syncs
 */

#ifdef ENABLE_SYNC

#include "integration_test_utils.h"
#include "mock_listeners.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

using namespace sdk_test;
using namespace testing;

/**
 * @brief Test fixture which initializates two sessions of the same account
 *
 * It offers functionality to perform operations from the Device Center.
 *
 * It initializes 2 MegaApi instance, the first (index 0) plays the role of the main device while
 * the second (index 1) is used as the remote Device Center.
 *
 */
class SdkTestDeviceCenter: public SdkTest
{
public:
    static constexpr auto MAX_TIMEOUT = 3min;
    handle mBackupID{UNDEF};

    void SetUp() override
    {
        SdkTest::SetUp();

        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
        ASSERT_NO_FATAL_FAILURE(ensureAccountDeviceName(megaApi[0].get()));

        // Initialize a second session with the same credentials
        ASSERT_NO_FATAL_FAILURE(initializeSecondSession());
    }

    bool resumeFromDeviceCenter(error expectedError = API_OK)
    {
        return doChangeFromDeviceCenter(DeviceCenterOperations::RESUME, expectedError);
    }

    bool pauseFromDeviceCenter(error expectedError = API_OK)
    {
        return doChangeFromDeviceCenter(DeviceCenterOperations::PAUSE, expectedError);
    }

    bool deleteFromDeviceCenter(error expectedError = API_OK,
                                MegaHandle destination = INVALID_HANDLE)
    {
        return doChangeFromDeviceCenter(DeviceCenterOperations::REMOVE, expectedError, destination);
    }

    const fs::path& getLocalFolder()
    {
        return localFolder.getPath();
    }

    bool waitForSyncStateFromMain(const MegaSync::SyncRunningState runState)
    {
        return waitForSyncState(megaApi[0].get(), mBackupID, runState, MegaSync::NO_SYNC_ERROR) !=
               nullptr;
    }

private:
    std::string localFolderName = getFilePrefix() + "dir";
    LocalTempDir localFolder{fs::current_path() / localFolderName};

    void initializeSecondSession()
    {
        megaApi.resize(megaApi.size() + 1);
        mApi.resize(mApi.size() + 1);
        configureTestInstance(1, mApi[0].email, mApi[0].pwd);

        NiceMock<MockRequestListener> loginTracker(megaApi[1].get());
        megaApi[1]->login(mApi[1].email.c_str(), mApi[1].pwd.c_str(), &loginTracker);
        ASSERT_TRUE(loginTracker.waitForFinishOrTimeout(MAX_TIMEOUT))
            << "Second session login failed";

        NiceMock<MockRequestListener> fetchNodesTracker(megaApi[1].get());
        megaApi[1]->fetchNodes(&fetchNodesTracker);
        ASSERT_TRUE(fetchNodesTracker.waitForFinishOrTimeout(MAX_TIMEOUT))
            << "Second session fetch nodes failed";
    }

    // Internal values to define the operations in the Device Center
    enum class DeviceCenterOperations
    {
        PAUSE,
        RESUME,
        REMOVE
    };

    bool doChangeFromDeviceCenter(const DeviceCenterOperations operation,
                                  error expectedError = API_OK,
                                  MegaHandle destination = INVALID_HANDLE)
    {
        NiceMock<MockRequestListener> reqTracker{megaApi[1].get()};
        reqTracker.setErrorExpectations(expectedError);

        switch (operation)
        {
            case DeviceCenterOperations::PAUSE:
                megaApi[1]->pauseFromBC(mBackupID, &reqTracker);
                break;
            case DeviceCenterOperations::RESUME:
                megaApi[1]->resumeFromBC(mBackupID, &reqTracker);
                break;
            case DeviceCenterOperations::REMOVE:
                megaApi[1]->removeFromBC(mBackupID, destination, &reqTracker);
                break;
        }
        return reqTracker.waitForFinishOrTimeout(MAX_TIMEOUT);
    }
};

class SdkTestDeviceCenterFullSync: public SdkTestDeviceCenter
{
public:
    void SetUp() override
    {
        SdkTestDeviceCenter::SetUp();

        ASSERT_NO_FATAL_FAILURE(setupFullSync());
    }

    void TearDown() override
    {
        if (const std::unique_ptr<MegaSync> sync{megaApi[0]->getSyncByBackupId(mBackupID)}; sync)
        {
            removeSync(megaApi[0].get(), mBackupID);
        }
        SdkTestDeviceCenter::TearDown();
    }

private:
    void setupFullSync()
    {
        LOG_debug << "Creating a full account sync";
        const unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
        mBackupID =
            syncFolder(megaApi[0].get(), getLocalFolder().u8string(), rootnode->getHandle());
        ASSERT_NE(mBackupID, INVALID_HANDLE) << "Invalid full-sync ID";
    }
};

/**
 * @brief Exercises the pause, resume and remove Device Center operations from a second session
 */
TEST_F(SdkTestDeviceCenterFullSync, FullSyncOperations)
{
    static const auto logPre{getLogPrefix()};

    // Pause the sync from the second session
    LOG_debug << logPre << "Pause full-sync from the Device Center";
    ASSERT_TRUE(pauseFromDeviceCenter()) << "Failed to pause full-sync from the second session";

    ASSERT_TRUE(waitForSyncStateFromMain(MegaSync::RUNSTATE_SUSPENDED))
        << "Full-sync not paused after 30 seconds";

    // Wait a while (for the *!sds user attr to be updated and propagated in response).
    std::this_thread::sleep_for(std::chrono::seconds{5});

    // Resume the sync from the second session
    LOG_debug << logPre << "Resume full-sync from the Device Center";
    ASSERT_TRUE(resumeFromDeviceCenter()) << "Failed to resume full-sync from the second session";

    ASSERT_TRUE(waitForSyncStateFromMain(MegaSync::RUNSTATE_RUNNING))
        << "Full-sync not resumed after 30 seconds";

    // Wait a while (for the *!sds user attr to be updated and propagated in response).
    std::this_thread::sleep_for(std::chrono::seconds{5});

    // Delete the sync from the second session
    LOG_debug << logPre << "Remove full-sync from the Device Center";

    NiceMock<MockSyncListener> listener;
    const auto hasExpectedId = Pointee(Property(&MegaSync::getBackupId, mBackupID));
    std::promise<void> removed;
    EXPECT_CALL(listener, onSyncDeleted(_, hasExpectedId))
        .WillOnce(
            [&removed]
            {
                removed.set_value();
            });
    megaApi[0]->addListener(&listener);

    ASSERT_TRUE(deleteFromDeviceCenter()) << "Failed to delete full-sync from the second session";
    ASSERT_EQ(removed.get_future().wait_for(MAX_TIMEOUT), std::future_status::ready)
        << "Full-sync still exists after 3 minutes";

    megaApi[0]->removeListener(&listener);
}

/**
 * @brief Test fixture to test Backups from the Device Center.
 *
 * It configures a backup from the first account and a folder to potentially stotre backups once
 * removed.
 *
 * It inherits functionality to perform operations from the Device Center using a secondary account.
 */
class SdkTestDeviceCenterBackup: public SdkTestDeviceCenter
{
public:
    void SetUp() override
    {
        SdkTestDeviceCenter::SetUp();

        ASSERT_NO_FATAL_FAILURE(setupBackup());
        ASSERT_NO_FATAL_FAILURE(setupDestinationDirectory());
    }

    void TearDown() override
    {
        if (const std::unique_ptr<MegaSync> sync{megaApi[0]->getSyncByBackupId(mBackupID)}; sync)
        {
            removeSync(megaApi[0].get(), mBackupID);
        }
        SdkTestDeviceCenter::TearDown();
    }

    void duplicateDestinationBackupFolder()
    {
        // Get parent folder
        const unique_ptr<MegaNode> parentFolder{
            megaApi[0]->getNodeByHandle(mDestinationFolderHandle)};
        ASSERT_TRUE(parentFolder);

        // Create a folder in the destination with the same name as the backup
        const MegaHandle newFolder = createFolder(0, mBackupName.c_str(), parentFolder.get());
        ASSERT_NE(newFolder, INVALID_HANDLE) << "Invalid destination folder handle";

        // Ensure that the second client can see the new folder
        unique_ptr<MegaNode> newFolderNode;
        waitFor(
            [&api = megaApi[1], newFolder, &newFolderNode]()
            {
                newFolderNode.reset(api->getNodeByHandle(newFolder));
                return newFolderNode != nullptr;
            },
            120s);
        ASSERT_TRUE(newFolderNode) << "Second account can't see the new folder.";
    }

    bool deleteFromDeviceCenterAndArchive(error expectedError)
    {
        return deleteFromDeviceCenter(expectedError, mDestinationFolderHandle);
    }

private:
    const fs::path mDestinationFolderName{"BackupArchive"};
    const std::string mBackupName{"myBackup"};
    MegaHandle mDestinationFolderHandle{INVALID_HANDLE};

    void setupBackup()
    {
        LOG_debug << "Creating a backup";
        mBackupID = backupFolder(megaApi[0].get(), getLocalFolder().u8string(), mBackupName);
        ASSERT_NE(mBackupID, INVALID_HANDLE) << "Invalid Backup ID";
    }

    void setupDestinationDirectory()
    {
        const unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
        ASSERT_TRUE(rootnode) << "Account root node not available.";
        mDestinationFolderHandle =
            createFolder(0, mDestinationFolderName.u8string().c_str(), rootnode.get());
        ASSERT_NE(mDestinationFolderHandle, INVALID_HANDLE) << "Invalid destination folder handle";
    }
};

TEST_F(SdkTestDeviceCenterBackup, RemoveDestinationClash)
{
    static const auto logPre{getLogPrefix()};

    LOG_debug << logPre << "Duplicate destination folder to cause a clash.";
    duplicateDestinationBackupFolder();

    LOG_debug
        << logPre
        << "Try to remove backup from the second session and move the data to the destination.";
    ASSERT_TRUE(deleteFromDeviceCenterAndArchive(API_EEXIST))
        << "Backups should not have been removed.";
}

#endif // ENABLE_SYNC
