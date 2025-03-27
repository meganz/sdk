/**
 * @file SdkTestSyncPrevalidation_test.cpp
 * @brief This file is expected to contain the SdkTestSyncPrevalidation test cases.
 *
 * Test cases testing failures cover one possible failure for different code flows; i.e., some
 * failures can happen during precondition checks, others are specific for sync type (like an error
 * returned from MegaClient::prepareBackup()), and others are part of the
 * MegaClient::checkSyncConfig().
 */

#ifdef ENABLE_SYNC

#include "SdkTestSyncPrevalidation.h"

using namespace sdk_test;

/**
 * @test SdkTestSyncPrevalidation.PrevalidateSyncOK
 *
 * 1. Prevalidates a sync that should work correctly.
 * 2. Creates the sync afterwards for double-checking: it should work as well.
 */
TEST_F(SdkTestSyncPrevalidation, PrevalidateSyncOK)
{
    static const auto logPre = getLogPrefix();

    LOG_verbose << logPre << "Prevalidating sync";
    SyncFolderExpectations prevalidateExpectations{MegaRequest::TYPE_ADD_SYNC_PREVALIDATION,
                                                   API_OK,
                                                   MegaSync::NO_SYNC_ERROR};
    ASSERT_NO_FATAL_FAILURE(prevalidateSync(std::move(prevalidateExpectations)));

    LOG_verbose << logPre << "Sync prevalidated OK. Creating sync: it should work as well";
    ASSERT_NO_FATAL_FAILURE(createSync());
}

/**
 * @test SdkTestSyncPrevalidation.PrevalidateSyncFailureAlreadyExists
 *
 * 1. Creates a sync.
 * 2. Prevalidates the sync: it should fail as it already exists.
 * 3. Tries to create the sync afterwards for double checking: it should fail as well.
 */
TEST_F(SdkTestSyncPrevalidation, PrevalidateSyncFailureAlreadyExists)
{
    static const auto logPre = getLogPrefix();

    LOG_verbose << logPre << "Creating sync";
    ASSERT_NO_FATAL_FAILURE(createSync());

    LOG_verbose << logPre << "Prevalidating sync over an existing sync: should fail";
    SyncFolderExpectations prevalidateExpectations{MegaRequest::TYPE_ADD_SYNC_PREVALIDATION,
                                                   API_EEXIST,
                                                   MegaSync::ACTIVE_SYNC_SAME_PATH};
    ASSERT_NO_FATAL_FAILURE(prevalidateSync(std::move(prevalidateExpectations)));

    LOG_verbose << logPre
                << "Trying to create a sync over an existing sync: should have same result as with "
                   "prevalidation";
    SyncFolderExpectations expectations{MegaRequest::TYPE_ADD_SYNC,
                                        API_EEXIST,
                                        MegaSync::ACTIVE_SYNC_SAME_PATH};
    ASSERT_NO_FATAL_FAILURE(createSync(std::move(expectations)));
}

/**
 * @test SdkTestSyncPrevalidation.PrevalidateSyncFailureNoRemotePath
 *
 * 1. Prevalidates a sync with a remote path that doesn't exist.
 * 2. Tries to create the sync afterwards for double checking: it should fail as well.
 */
TEST_F(SdkTestSyncPrevalidation, PrevalidateSyncFailureNoRemotePath)
{
    static const auto logPre = getLogPrefix();

    const std::string fakeRemotePath{"fakePath"};
    LOG_verbose << logPre << "Prevalidating sync over with a non existing remote path";
    SyncFolderExpectations prevalidateExpectations{MegaRequest::TYPE_ADD_SYNC_PREVALIDATION,
                                                   API_EARGS,
                                                   MegaSync::NO_SYNC_ERROR};
    ASSERT_NO_FATAL_FAILURE(prevalidateSync(std::move(prevalidateExpectations), fakeRemotePath));

    LOG_verbose << logPre
                << "Trying to create a sync with a non existing remote path: should have same "
                   "result as with prevalidation";
    SyncFolderExpectations expectations{MegaRequest::TYPE_ADD_SYNC,
                                        API_EARGS,
                                        MegaSync::NO_SYNC_ERROR};
    ASSERT_NO_FATAL_FAILURE(createSync(std::move(expectations), fakeRemotePath));
}

/**
 * @test SdkTestSyncPrevalidation.PrevalidateBackupOK
 *
 * 1. Prevalidates a backup that should work correctly.
 * 2. Creates the backup afterwards for double-checking: it should work as well.
 */
TEST_F(SdkTestSyncPrevalidation, PrevalidateBackupOK)
{
    static const auto logPre = getLogPrefix();

    LOG_verbose << logPre << "Prevalidating backup";
    SyncFolderExpectations prevalidateExpectations{MegaRequest::TYPE_ADD_SYNC_PREVALIDATION,
                                                   API_OK,
                                                   MegaSync::NO_SYNC_ERROR};
    ASSERT_NO_FATAL_FAILURE(prevalidateBackup(std::move(prevalidateExpectations)));

    LOG_verbose << logPre << "Backup prevalidated OK. Creating sync: it should work as well";
    ASSERT_NO_FATAL_FAILURE(createBackup());
}

/**
 * @test SdkTestSyncPrevalidation.PrevalidateSyncFailureAlreadyExists
 *
 * 1. Creates a backup.
 * 2. Prevalidates the backup: it should fail as it already exists.
 * 3. Tries to create the backup afterwards for double checking: it should fail as well.
 *
 * @note Unlike PrevalidateSyncFailureAlreadyExists, whose checks are done at
 * MegaClient::checkSyncConfig(), this logic is checked within MegaClient::preparebackup() (called
 * before checkSyncConfig()).
 */
TEST_F(SdkTestSyncPrevalidation, PrevalidateBackupFailureAlreadyExists)
{
    static const auto logPre = getLogPrefix();

    LOG_verbose << logPre << "Creating backup";
    ASSERT_NO_FATAL_FAILURE(createBackup());

    LOG_verbose << logPre << "Prevalidating backup over an existing backup: should fail";
    SyncFolderExpectations prevalidateExpectations{MegaRequest::TYPE_ADD_SYNC_PREVALIDATION,
                                                   API_EACCESS,
                                                   MegaSync::NO_SYNC_ERROR};
    ASSERT_NO_FATAL_FAILURE(prevalidateBackup(std::move(prevalidateExpectations)));

    LOG_verbose
        << logPre
        << "Trying to create a backup over an existing backup: should have same result as with "
           "prevalidation";
    SyncFolderExpectations expectations{MegaRequest::TYPE_ADD_SYNC,
                                        API_EACCESS,
                                        MegaSync::NO_SYNC_ERROR};
    ASSERT_NO_FATAL_FAILURE(createBackup(std::move(expectations)));
}

#endif // ENABLE_SYNC
