/**
 * @file SdkTestSyncPrevalidation.h
 * @brief This file is expected to contain SdkTestSyncPrevalidation declaration.
 */

#ifdef ENABLE_SYNC

#ifndef INCLUDE_INTEGRATION_SDKTESTSYNCPREVALIDATION_H_
#define INCLUDE_INTEGRATION_SDKTESTSYNCPREVALIDATION_H_

#include "SdkTestSyncNodesOperations.h"

namespace sdk_test
{

/**
 * @brief Contains the retrieved backupId and the requestWasFinished flag.
 * If the request didn't met the expectations, requestWasFinished would be false.
 */
using SyncFolderResult = std::pair<handle /* backupId */, bool /* requestWasFinished */>;

/**
 * @brief Struct containing the expectations for the MegaApi::syncFolder() /
 * MegaApi::prevalidateSyncFolder() requests.
 */
struct SyncFolderExpectations
{
    int expectedReqType{MegaRequest::TYPE_ADD_SYNC};
    error expectedError{API_OK};
    int expectedSyncError{MegaSync::NO_SYNC_ERROR};
};

/**
 * @class SdkTestSyncPrevalidation
 * @brief Test fixture designed to test MegaApi::syncFolder() and MegaApi::prevalidateSyncFolder().
 *
 * @note The methods to create a sync/backup store the backupId as part of the state. Any
 * sync/backup created with these methods should be removed if a new one is going to be created
 * within the same test case. Otherwise there's no need to remove them as that's done as part of the
 * TearDown.
 */
class SdkTestSyncPrevalidation: public SdkTestSyncNodesOperations
{
public:
    bool createSyncOnSetup() const override
    {
        return false;
    }

    void createSync(SyncFolderExpectations&& expectedValues = {},
                    const std::string& remotePath = DEFAULT_SYNC_REMOTE_PATH);

    void createBackup(SyncFolderExpectations&& expectedValues = {});

    void prevalidateSync(SyncFolderExpectations&& expectedValues,
                         const std::string& remotePath = DEFAULT_SYNC_REMOTE_PATH);

    void prevalidateBackup(SyncFolderExpectations&& expectedValues);

private:
    static const std::string DEFAULT_BACKUP_NAME;

    void createSyncOrBackup(SyncFolderExpectations&& expectedValues,
                            std::function<SyncFolderResult(SyncFolderExpectations&&)>&& completion);

    void prevalidateSyncOrBackup(
        SyncFolderExpectations&& expectedValues,
        std::function<SyncFolderResult(SyncFolderExpectations&&)>&& completion);
};

} // namespace sdk_test

#endif // INCLUDE_INTEGRATION_SDKTESTSYNCPREVALIDATION_H_
#endif // ENABLE_SYNC
