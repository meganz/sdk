#include "integration_test_utils.h"

#include "mega/logging.h"
#include "sdk_test_utils.h"

#include <chrono>

namespace sdk_test
{
using namespace mega;
using namespace std::chrono_literals;

/**
 * @brief Aux implementation to generalize on how to get the sync
 */
static std::unique_ptr<::mega::MegaSync> waitForSyncState(MegaSync::SyncRunningState runState,
                                                          MegaSync::Error err,
                                                          std::function<MegaSync*()>&& syncGetter)
{
    std::unique_ptr<MegaSync> sync;
    waitFor(
        [syncGetter = std::move(syncGetter), &sync, runState, err]() -> bool
        {
            sync.reset(syncGetter());
            return (sync && sync->getRunState() == runState && sync->getError() == err);
        },
        30s);

    if (sync)
    {
        bool areTheExpectedStateAndError =
            sync->getRunState() == runState && sync->getError() == err;
        LOG_debug << "sync exists with the "
                  << (areTheExpectedStateAndError ? "expected" : "UNEXPECTED")
                  << " state: " << sync->getRunState() << " and error: " << sync->getError();
        return areTheExpectedStateAndError ? std::move(sync) : nullptr;
    }
    else
    {
        LOG_debug << "sync is null";
        return nullptr; // signal that the sync never reached the expected/required state
    }
}

std::unique_ptr<::mega::MegaSync> waitForSyncState(MegaApi* megaApi,
                                                   MegaNode* remoteNode,
                                                   MegaSync::SyncRunningState runState,
                                                   MegaSync::Error err)
{
    return waitForSyncState(runState,
                            err,
                            [&megaApi, &remoteNode]() -> MegaSync*
                            {
                                return megaApi->getSyncByNode(remoteNode);
                            });
}

std::unique_ptr<::mega::MegaSync> waitForSyncState(MegaApi* megaApi,
                                                   handle backupID,
                                                   MegaSync::SyncRunningState runState,
                                                   MegaSync::Error err)
{
    return waitForSyncState(runState,
                            err,
                            [&megaApi, backupID]() -> MegaSync*
                            {
                                return megaApi->getSyncByBackupId(backupID);
                            });
}

}
