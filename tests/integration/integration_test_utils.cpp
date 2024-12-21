#include "integration_test_utils.h"

#include "integration/mock_listeners.h"
#include "mega/logging.h"
#include "megautils.h"
#include "sdk_test_utils.h"

#include <chrono>

namespace sdk_test
{
using namespace mega;
using namespace std::chrono_literals;
using namespace testing;

static constexpr auto MAX_TIMEOUT = 3min; // Timeout for operations in this file

#ifdef ENABLE_SYNC

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

handle syncFolder(MegaApi* megaApi,
                  const std::string& localRootPath,
                  const MegaHandle remoteRootHandle)
{
    if (!megaApi)
        return mega::UNDEF;

    using namespace testing;
    NiceMock<MockRequestListener> rl;
    const auto expectedErr = Pointee(Property(&MegaError::getErrorCode, API_OK));
    const auto& expectedReqType =
        Pointee(Property(&MegaRequest::getType, MegaRequest::TYPE_ADD_SYNC));
    const auto expectedSyncErr =
        Pointee(Property(&::mega::MegaError::getSyncError, MegaSync::NO_SYNC_ERROR));
    handle backupId = UNDEF;
    EXPECT_CALL(rl, onRequestFinish(_, expectedReqType, AllOf(expectedErr, expectedSyncErr)))
        .WillOnce(
            [&backupId, &rl](MegaApi*, MegaRequest* req, MegaError*)
            {
                backupId = req->getParentHandle();
                rl.markAsFinished();
            });

    megaApi->syncFolder(MegaSync::TYPE_TWOWAY,
                        localRootPath.c_str(),
                        nullptr,
                        remoteRootHandle,
                        nullptr,
                        &rl);
    rl.waitForFinishOrTimeout(MAX_TIMEOUT);
    if (backupId == UNDEF)
        return UNDEF;

    // Ensure it is running
    std::unique_ptr<MegaSync> sync = sdk_test::waitForSyncState(megaApi,
                                                                backupId,
                                                                MegaSync::RUNSTATE_RUNNING,
                                                                MegaSync::NO_SYNC_ERROR);
    if (!sync)
        return UNDEF;
    return backupId;
}

bool removeSync(MegaApi* megaApi, const handle backupID)
{
    NiceMock<MockRequestListener> reqListener;
    reqListener.setErrorExpectations(API_OK);
    megaApi->removeSync(backupID, &reqListener);
    return reqListener.waitForFinishOrTimeout(MAX_TIMEOUT);
}

bool setSyncRunState(MegaApi* megaApi,
                     const handle backupID,
                     const MegaSync::SyncRunningState state)
{
    NiceMock<MockRequestListener> reqListener;
    reqListener.setErrorExpectations(API_OK);
    megaApi->setSyncRunState(backupID, state, &reqListener);
    return reqListener.waitForFinishOrTimeout(MAX_TIMEOUT);
}

bool resumeSync(MegaApi* megaApi, const handle backupID)
{
    return setSyncRunState(megaApi, backupID, MegaSync::SyncRunningState::RUNSTATE_RUNNING);
}

bool suspendSync(MegaApi* megaApi, const handle backupID)
{
    return setSyncRunState(megaApi, backupID, MegaSync::SyncRunningState::RUNSTATE_SUSPENDED);
}

bool disableSync(MegaApi* megaApi, const handle backupID)
{
    return setSyncRunState(megaApi, backupID, MegaSync::SyncRunningState::RUNSTATE_DISABLED);
}

std::vector<std::unique_ptr<MegaSyncStall>> getStalls(MegaApi* megaApi)
{
    if (megaApi == nullptr)
        return {};

    NiceMock<MockRequestListener> reqList;
    const auto expectedErr = Pointee(Property(&MegaError::getErrorCode, API_OK));
    std::vector<std::unique_ptr<MegaSyncStall>> stalls;
    EXPECT_CALL(reqList, onRequestFinish(_, _, expectedErr))
        .WillOnce(
            [&stalls, &reqList](MegaApi*, MegaRequest* request, MegaError*)
            {
                if (auto list = request->getMegaSyncStallList(); list != nullptr)
                    stalls = toSyncStallVector(*list);
                reqList.markAsFinished();
            });
    megaApi->getMegaSyncStallList(&reqList);
    if (!reqList.waitForFinishOrTimeout(MAX_TIMEOUT))
        return {};
    return stalls;
}

#endif

std::optional<std::vector<std::string>> getCloudFirstChildrenNames(MegaApi* megaApi,
                                                                   const MegaHandle nodeHandle)
{
    if (!megaApi || nodeHandle == UNDEF)
        return {};
    std::unique_ptr<MegaNode> rootNode{megaApi->getNodeByHandle(nodeHandle)};
    if (!rootNode)
        return {};
    std::unique_ptr<MegaNodeList> childrenNodeList{megaApi->getChildren(rootNode.get())};
    if (!childrenNodeList)
        return {};
    return toNamesVector(*childrenNodeList);
}
}
