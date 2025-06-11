/**
 * @file backup_sync_operations_test.cpp
 * @brief This file contains tests for the public interfaces available to manage backups or syncs,
 * stop them and archive or remove a deconfigured backup.
 */

#ifdef ENABLE_SYNC

#include "integration_test_utils.h"
#include "mock_listeners.h"
#include "SdkTest_test.h"

using namespace sdk_test;
using namespace testing;

/**
 * @brief SdkTestBackup class implementing basic operations for Backups and Syncs (to be extended).
 * It initializes one testing account and ensures that the device name is configured.
 */
class SdkTestBackupSync: public SdkTest
{
private:
    const fs::path mLocalFolderName{getFilePrefix() + "dir"};
    const LocalTempDir mLocalFolder{fs::current_path() / mLocalFolderName};

protected:
    MegaHandle mBackupID{INVALID_HANDLE};
    const std::string mBackupName{"myBackup"};

public:
    static constexpr auto mMaxTimeout{3min};

    void SetUp() override
    {
        SdkTest::SetUp();
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
        ASSERT_NO_FATAL_FAILURE(ensureAccountDeviceName(megaApi[0].get()));
    }

    const fs::path& getLocalFolder() const
    {
        return mLocalFolder.getPath();
    }

    void setupBackupSync()
    {
        LOG_debug << "Creating a backup";
        ASSERT_EQ(mBackupID, INVALID_HANDLE) << "There is already a backup/sync created.";
        mBackupID = backupFolder(megaApi[0].get(), getLocalFolder().u8string(), mBackupName);
        ASSERT_NE(mBackupID, INVALID_HANDLE) << "Invalid Backup ID";
    }

    void removeSync()
    {
        ASSERT_NE(mBackupID, INVALID_HANDLE) << "Cant't remove backup/sync. Invalid Backup ID";
        if (const std::unique_ptr<MegaSync> sync{megaApi[0]->getSyncByBackupId(mBackupID)}; sync)
        {
            ASSERT_TRUE(::removeSync(megaApi[0].get(), mBackupID));
        }
    }
};

/**
 * @brief The SdkTestBackupOperations class
 *
 * Test fixture that creates a backup and a destination directory to archive backups when moved.
 * It offers functionality to test a clash when archiving a backup.
 */
class SdkTestBackupOperations: public SdkTestBackupSync
{
private:
    MegaHandle mBackupRootHandle{INVALID_HANDLE};
    const fs::path mDestinationFolderName{"BackupArchive"};
    MegaHandle mDestinationFolderHandle{INVALID_HANDLE};

public:
    void SetUp() override
    {
        SdkTestBackupSync::SetUp();
        ASSERT_NO_FATAL_FAILURE(setupBackupSync());
        ASSERT_NO_FATAL_FAILURE(setupDestinationDirectory());
        const std::unique_ptr<MegaSync> sync{megaApi[0]->getSyncByBackupId(mBackupID)};
        ASSERT_TRUE(sync);
        mBackupRootHandle = sync->getMegaHandle();
    }

    void TearDown() override
    {
        if (const std::unique_ptr<MegaSync> sync{megaApi[0]->getSyncByBackupId(mBackupID)}; sync)
        {
            ::removeSync(megaApi[0].get(), mBackupID);
        }
        SdkTestBackupSync::TearDown();
    }

    void setupDestinationDirectory()
    {
        unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
        ASSERT_TRUE(rootnode) << "Account root node not available.";
        mDestinationFolderHandle =
            createFolder(0, mDestinationFolderName.u8string().c_str(), rootnode.get());
        ASSERT_NE(mDestinationFolderHandle, INVALID_HANDLE) << "Invalid destination folder handle";
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
    }

    bool removeBackupNode(error expectedError = API_OK)
    {
        return moveOrRemoveBackupNode(expectedError);
    }

    bool archiveBackupNode(error expectedError = API_OK)
    {
        return moveOrRemoveBackupNode(expectedError, mDestinationFolderHandle);
    }

private:
    bool moveOrRemoveBackupNode(error expectedError = API_OK,
                                MegaHandle destination = INVALID_HANDLE)
    {
        NiceMock<MockRequestListener> reqTracker{megaApi[0].get()};
        reqTracker.setErrorExpectations(expectedError);
        megaApi[0]->moveOrRemoveDeconfiguredBackupNodes(mBackupRootHandle,
                                                        destination,
                                                        &reqTracker);
        return reqTracker.waitForFinishOrTimeout(mMaxTimeout);
    }
};

TEST_F(SdkTestBackupOperations, RemoveDestinationClash)
{
    static const auto logPre{getLogPrefix()};

    LOG_debug << logPre << "Duplicate destination folder to cause a clash.";
    duplicateDestinationBackupFolder();

    LOG_debug << logPre << "Remove backup sync";
    removeSync();

    LOG_debug << logPre << "Try to move backup root node to the cloud";
    ASSERT_TRUE(archiveBackupNode(API_EEXIST)) << "Desination node should already exist and fail.";

    LOG_debug << logPre << "Remove backup contents";
    ASSERT_TRUE(removeBackupNode()) << "Can't remove backup contents.";
}

#endif
