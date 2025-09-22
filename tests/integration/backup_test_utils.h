/**
 * @file backup_test_utils.h
 * @brief This file defines a test fixture that involves basic operations on backup syncs.
 */
#ifdef ENABLE_SYNC

#include "integration_test_utils.h"
#include "mock_listeners.h"
#include "SdkTest_test.h"

using namespace sdk_test;
using namespace testing;

/**
 * @class SdkTestBackup
 * @brief Test fixture that allow to perform create, suspend, resume, and remove a sync backup.
 *
 * - The local folder is created in the current working directory with the name:
 *   `TestSuite_TestName_dir`.
 * - The backup name in the cloud is set with the name: `myBackup`.
 */
class SdkTestBackup: public SdkTest
{
public:
    static constexpr auto mMaxTimeout{3min};

    void SetUp() override
    {
        SdkTest::SetUp();
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
        ASSERT_NO_FATAL_FAILURE(ensureAccountDeviceName(megaApi[0].get()));
    }

    void createBackupSync()
    {
        ASSERT_EQ(mBackupID, INVALID_HANDLE) << "There is already a backup/sync created.";
        mBackupID = backupFolder(megaApi[0].get(), getLocalFolderPath().u8string(), mBackupName);
        ASSERT_NE(mBackupID, INVALID_HANDLE) << "Cannot create Backup sync. Invalid Backup ID";
    }

    void removeBackupSync()
    {
        ASSERT_NE(mBackupID, INVALID_HANDLE) << "Cant't remove backup/sync. Invalid Backup ID";
        if (const std::unique_ptr<MegaSync> sync{megaApi[0]->getSyncByBackupId(mBackupID)}; sync)
        {
            ASSERT_TRUE(::removeSync(megaApi[0].get(), mBackupID))
                << "Cannot remove backup sync"
                << ". BackupID (" << toHandle(mBackupID) << ")";
            mBackupID = INVALID_HANDLE;
        }
    }

    void suspendBackupSync()
    {
        ASSERT_NE(mBackupID, INVALID_HANDLE) << "Cant't suspend backup/sync. Invalid Backup ID";
        ASSERT_TRUE(setSyncRunState(megaApi[0].get(),
                                    mBackupID,
                                    MegaSync::SyncRunningState::RUNSTATE_SUSPENDED))
            << "Cannot suspend backup sync"
            << ". BackupID (" << toHandle(mBackupID) << ")";
    }

    void resumeBackupSync()
    {
        ASSERT_NE(mBackupID, INVALID_HANDLE) << "Cant't resume backup/sync. Invalid Backup ID";
        ASSERT_TRUE(setSyncRunState(megaApi[0].get(),
                                    mBackupID,
                                    MegaSync::SyncRunningState::RUNSTATE_RUNNING))
            << "Cannot resume backup sync"
            << ". BackupID (" << toHandle(mBackupID) << ")";
    }

    const fs::path& getLocalFolderPath() const
    {
        return mLocalTmpDir.getPath();
    }

    MegaHandle getBackupId() const
    {
        return mBackupID;
    }

    std::string getBackupName() const
    {
        return mBackupName;
    }

private:
    MegaHandle mBackupID{INVALID_HANDLE};
    const std::string mBackupName{"myBackup"};
    const fs::path mLocalFolderName{getFilePrefix() + "dir"};
    const LocalTempDir mLocalTmpDir{fs::current_path() / mLocalFolderName};
};
#endif