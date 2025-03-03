/**
 * @file SdkTestSyncPrevalidation.cpp
 * @brief This file is expected to contain SdkTestSyncPrevalidation class definition.
 */

#ifdef ENABLE_SYNC

#include "SdkTestSyncPrevalidation.h"

#include "integration/mock_listeners.h"

using namespace sdk_test;
using namespace testing;

namespace
{
using namespace std::chrono_literals;
constexpr auto MAX_TIMEOUT = 3min;

struct SyncFolderParams
{
    MegaSync::SyncType syncType;
    std::string localRootPath;
    std::string backupName;
    MegaHandle remoteRootHandle;
    std::string driveRootIfExternal;
};

SyncFolderResult syncFolderRequestWithExpectations(
    MegaApi* const megaApi,
    SyncFolderExpectations&& expectedValues,
    std::function<void(NiceMock<MockRequestListener>&)>&& syncRequest)

{
    if (!megaApi)
        return {UNDEF, false};

    NiceMock<MockRequestListener> rl{megaApi};

    handle backupId = UNDEF;
    auto setBackupId = [&backupId](const MegaRequest& req)
    {
        backupId = req.getParentHandle();
    };

    rl.setErrorExpectations(expectedValues.expectedError,
                            expectedValues.expectedSyncError,
                            expectedValues.expectedReqType,
                            std::move(setBackupId));

    syncRequest(rl);

    const auto finished = rl.waitForFinishOrTimeout(MAX_TIMEOUT);
    return {backupId, finished};
}

SyncFolderResult syncFolderRequest(MegaApi* const megaApi,
                                   SyncFolderParams&& params,
                                   SyncFolderExpectations&& expectedValues)
{
    auto syncRequest = [&megaApi, params = std::move(params)](auto& requestListener)
    {
        megaApi->syncFolder(params.syncType,
                            params.localRootPath,
                            params.backupName,
                            params.remoteRootHandle,
                            params.driveRootIfExternal,
                            &requestListener);
    };
    return syncFolderRequestWithExpectations(megaApi,
                                             std::move(expectedValues),
                                             std::move(syncRequest));
}

SyncFolderResult prevalidateSyncFolderRequest(MegaApi* const megaApi,
                                              SyncFolderParams&& params,
                                              SyncFolderExpectations&& expectedValues)
{
    auto syncRequest = [&megaApi, params = std::move(params)](auto& requestListener)
    {
        megaApi->prevalidateSyncFolder(params.syncType,
                                       params.localRootPath,
                                       params.backupName,
                                       params.remoteRootHandle,
                                       params.driveRootIfExternal,
                                       &requestListener);
    };
    return syncFolderRequestWithExpectations(megaApi,
                                             std::move(expectedValues),
                                             std::move(syncRequest));
}

SyncFolderResult syncFolderWithExpects(MegaApi* const megaApi,
                                       const std::string& localRootPath,
                                       const MegaHandle remoteRootHandle,
                                       SyncFolderExpectations&& expectedValues)
{
    SyncFolderParams params{MegaSync::TYPE_TWOWAY, localRootPath, "", remoteRootHandle, ""};
    return syncFolderRequest(megaApi, std::move(params), std::move(expectedValues));
}

SyncFolderResult backupFolderWithExpects(MegaApi* const megaApi,
                                         const std::string& localRootPath,
                                         const std::string& backupName,
                                         SyncFolderExpectations&& expectedValues)
{
    SyncFolderParams params{MegaSync::TYPE_BACKUP, localRootPath, backupName, UNDEF, ""};
    return syncFolderRequest(megaApi, std::move(params), std::move(expectedValues));
}

SyncFolderResult prevalidateSyncWithExpects(MegaApi* const megaApi,
                                            const std::string& localRootPath,
                                            const MegaHandle remoteRootHandle,
                                            SyncFolderExpectations&& expectedValues)
{
    SyncFolderParams params{MegaSync::TYPE_TWOWAY, localRootPath, "", remoteRootHandle, ""};
    return prevalidateSyncFolderRequest(megaApi, std::move(params), std::move(expectedValues));
}

SyncFolderResult prevalidateBackupWithExpects(MegaApi* const megaApi,
                                              const std::string& localRootPath,
                                              const std::string& backupName,
                                              SyncFolderExpectations&& expectedValues)
{
    SyncFolderParams params{MegaSync::TYPE_BACKUP, localRootPath, backupName, UNDEF, ""};
    return prevalidateSyncFolderRequest(megaApi, std::move(params), std::move(expectedValues));
}
} // namespace

// SdkTestSyncPrevalidation

const std::string SdkTestSyncPrevalidation::DEFAULT_BACKUP_NAME{"myBackup"};

void SdkTestSyncPrevalidation::createSyncOrBackup(
    SyncFolderExpectations&& expectedValues,
    std::function<SyncFolderResult(SyncFolderExpectations&&)>&& completion)
{
    const auto expectedOk = (expectedValues.expectedError == API_OK &&
                             expectedValues.expectedSyncError == MegaSync::NO_SYNC_ERROR);
    if (expectedOk)
    {
        ASSERT_EQ(mBackupId, UNDEF);
    }

    const auto [backupId, result] = completion(std::move(expectedValues));
    mBackupId = backupId;

    ASSERT_TRUE(result);

    if (expectedOk)
    {
        ASSERT_NE(mBackupId, UNDEF);
    }
    else
    {
        ASSERT_EQ(mBackupId, UNDEF);
    }
}

void SdkTestSyncPrevalidation::createSync(SyncFolderExpectations&& expectedValues,
                                          const std::string& remotePath)
{
    auto completion = [this,
                       &remotePath](SyncFolderExpectations&& expectedValues) -> SyncFolderResult
    {
        return syncFolderWithExpects(megaApi[0].get(),
                                     getLocalTmpDirU8string(),
                                     getNodeHandleByPath(remotePath),
                                     std::move(expectedValues));
    };
    ASSERT_NO_FATAL_FAILURE(createSyncOrBackup(std::move(expectedValues), std::move(completion)));
}

void SdkTestSyncPrevalidation::createBackup(SyncFolderExpectations&& expectedValues)
{
    auto completion = [this](SyncFolderExpectations&& expectedValues) -> SyncFolderResult
    {
        return backupFolderWithExpects(megaApi[0].get(),
                                       getLocalTmpDirU8string(),
                                       DEFAULT_BACKUP_NAME,
                                       std::move(expectedValues));
    };
    ASSERT_NO_FATAL_FAILURE(createSyncOrBackup(std::move(expectedValues), std::move(completion)));
}

void SdkTestSyncPrevalidation::prevalidateSyncOrBackup(
    SyncFolderExpectations&& expectedValues,
    std::function<SyncFolderResult(SyncFolderExpectations&&)>&& completion)
{
    const auto [backupId, result] = completion(std::move(expectedValues));

    ASSERT_TRUE(result);
    ASSERT_EQ(backupId, UNDEF);
}

void SdkTestSyncPrevalidation::prevalidateSync(SyncFolderExpectations&& expectedValues,
                                               const std::string& remotePath)
{
    auto completion = [this,
                       &remotePath](SyncFolderExpectations&& expectedValues) -> SyncFolderResult
    {
        return prevalidateSyncWithExpects(megaApi[0].get(),
                                          getLocalTmpDirU8string(),
                                          getNodeHandleByPath(remotePath),
                                          std::move(expectedValues));
    };
    ASSERT_NO_FATAL_FAILURE(
        prevalidateSyncOrBackup(std::move(expectedValues), std::move(completion)));
}

void SdkTestSyncPrevalidation::prevalidateBackup(SyncFolderExpectations&& expectedValues)
{
    auto completion = [this](SyncFolderExpectations&& expectedValues) -> SyncFolderResult
    {
        return prevalidateBackupWithExpects(megaApi[0].get(),
                                            getLocalTmpDirU8string(),
                                            DEFAULT_BACKUP_NAME,
                                            std::move(expectedValues));
    };
    ASSERT_NO_FATAL_FAILURE(
        prevalidateSyncOrBackup(std::move(expectedValues), std::move(completion)));
}

#endif // ENABLE_SYNC
