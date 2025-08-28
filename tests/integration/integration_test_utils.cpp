#include "integration_test_utils.h"

#include "gtest_common.h"
#include "integration/mock_listeners.h"
#include "mega/logging.h"
#include "megautils.h"
#include "sdk_test_utils.h"

#include <chrono>

namespace sdk_test
{
using namespace mega;
using namespace testing;
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

bool waitForSyncStallState(MegaApi* const megaApi)
{
    const auto isSyncStalledPred = [&megaApi]() -> bool
    {
        return isSyncStalled(megaApi);
    };
    return waitFor(isSyncStalledPred, 10s);
}

static handle createSyncAux(MegaApi* megaApi,
                            const MegaSync::SyncType syncType,
                            const std::string& localRootPath,
                            const MegaHandle remoteRootHandle,
                            const std::string& backupName)
{
    if (!megaApi)
        return UNDEF;

    if (syncType == MegaSync::TYPE_BACKUP && remoteRootHandle != UNDEF)
        return UNDEF;

    using namespace testing;
    NiceMock<MockRequestListener> rl{megaApi};

    handle backupId = UNDEF;
    auto setBackupId = [&backupId](const MegaRequest& req)
    {
        backupId = req.getParentHandle();
    };

    rl.setErrorExpectations(API_OK,
                            MegaSync::NO_SYNC_ERROR,
                            MegaRequest::TYPE_ADD_SYNC,
                            std::move(setBackupId));

    megaApi->syncFolder(syncType,
                        localRootPath.c_str(),
                        backupName.empty() ? nullptr : backupName.c_str(),
                        remoteRootHandle,
                        nullptr,
                        &rl);
    rl.waitForFinishOrTimeout(MAX_TIMEOUT);
    if (backupId == UNDEF)
        return UNDEF;

    std::unique_ptr<MegaSync> sync =
        waitForSyncState(megaApi, backupId, MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    if (!sync)
        return UNDEF;
    return backupId;
}

handle syncFolder(MegaApi* megaApi,
                  const std::string& localRootPath,
                  const MegaHandle remoteRootHandle)
{
    return createSyncAux(megaApi, MegaSync::TYPE_TWOWAY, localRootPath, remoteRootHandle, "");
}

handle backupFolder(MegaApi* megaApi,
                    const std::string& localRootPath,
                    const std::string& backupName)
{
    return createSyncAux(megaApi, MegaSync::TYPE_BACKUP, localRootPath, UNDEF, backupName);
}

bool removeSync(MegaApi* megaApi, const handle backupID)
{
    NiceMock<MockRequestListener> reqListener{megaApi};
    reqListener.setErrorExpectations(API_OK);
    megaApi->removeSync(backupID, &reqListener);
    return reqListener.waitForFinishOrTimeout(MAX_TIMEOUT);
}

bool setSyncRunState(MegaApi* megaApi,
                     const handle backupID,
                     const MegaSync::SyncRunningState state)
{
    NiceMock<MockRequestListener> reqListener{megaApi};
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

bool isSyncStalled(MegaApi* const megaApi)
{
    if (megaApi == nullptr)
        return false;

    return megaApi->isSyncStalled();
}

std::vector<std::unique_ptr<MegaSyncStall>> getStalls(MegaApi* megaApi)
{
    if (megaApi == nullptr)
        return {};

    NiceMock<MockRequestListener> reqList{megaApi};
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

std::pair<std::optional<std::vector<std::string>>, std::unique_ptr<MegaNodeList>>

    getCloudFirstChildren(MegaApi* megaApi, const MegaHandle nodeHandle)
{
    if (!megaApi || nodeHandle == UNDEF)
        return {std::nullopt, nullptr};
    std::unique_ptr<MegaNode> rootNode{megaApi->getNodeByHandle(nodeHandle)};
    if (!rootNode)
        return {std::nullopt, nullptr};
    std::unique_ptr<MegaNodeList> childrenNodeList{megaApi->getChildren(rootNode.get())};
    if (!childrenNodeList)
        return {std::nullopt, nullptr};

    auto namesVector = toNamesVector(*childrenNodeList);
    if (namesVector.size() != static_cast<size_t>(childrenNodeList->size()))
    {
        assert(false && "getCloudFirstChildren: invalid names vector size ");
        return {std::nullopt, nullptr};
    }

    return {namesVector, std::move(childrenNodeList)};
}

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

void getDeviceNames(MegaApi* megaApi, std::unique_ptr<MegaStringMap>& output)
{
    NiceMock<MockRequestListener> rl{megaApi};
    const auto expectedErr = Pointee(Property(&MegaError::getErrorCode, API_OK));
    EXPECT_CALL(rl, onRequestFinish(_, _, expectedErr))
        .WillOnce(
            [&output, &rl](MegaApi*, MegaRequest* req, MegaError*)
            {
                output.reset(req->getMegaStringMap()->copy());
                rl.markAsFinished();
            });
    megaApi->getUserAttribute(MegaApi::USER_ATTR_DEVICE_NAMES, &rl);
    ASSERT_TRUE(rl.waitForFinishOrTimeout(3min));
}

void ensureAccountDeviceName(MegaApi* megaApi)
{
    std::unique_ptr<MegaStringMap> devices;
    ASSERT_NO_FATAL_FAILURE(getDeviceNames(megaApi, devices));
    ASSERT_TRUE(devices);

    // There are already available devices
    if (devices->size() != 0)
        return;

    const std::string deviceName = "Jenkins " + getCurrentTimestamp(true);
    const std::string deviceId = megaApi->getDeviceId();
    devices->set(deviceId.c_str(), deviceName.c_str());
    NiceMock<MockRequestListener> rl{megaApi};
    rl.setErrorExpectations(API_OK);
    megaApi->setUserAttribute(MegaApi::USER_ATTR_DEVICE_NAMES, devices.get(), &rl);
    ASSERT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
}

std::optional<int> downloadNode(MegaApi* megaApi,
                                MegaNode* node,
                                const std::filesystem::path& fsPath,
                                bool pathIsFolder,
                                const std::chrono::seconds timeoutInSecs,
                                const int collisionCheck,
                                const int collisionResolution,
                                const char* customName,
                                const char* appData,
                                const bool startFirst,
                                MegaCancelToken* cancelToken,
                                const bool undelete)
{
    if (!megaApi || !node)
    {
        LOG_err << "test_utils(downloadFile): EARGS";
        return std::nullopt;
    }

    std::optional<int> err{std::nullopt};
    testing::NiceMock<MockMegaTransferListener> mtl{megaApi};
    EXPECT_CALL(mtl, onTransferFinish)
        .WillOnce(
            [&mtl, &err](MegaApi*, MegaTransfer*, MegaError* error)
            {
                err = error ? error->getErrorCode() : API_EINTERNAL;
                mtl.markAsFinished();
            });
    std::string downLoadPath{fsPath.u8string()};
    if (pathIsFolder && downLoadPath.back() != std::filesystem::path::preferred_separator)
    {
        downLoadPath.push_back(std::filesystem::path::preferred_separator);
    }

    megaApi->startDownload(node,
                           downLoadPath.c_str(),
                           customName,
                           appData,
                           startFirst,
                           cancelToken,
                           collisionCheck,
                           collisionResolution,
                           undelete,
                           &mtl);

    if (!mtl.waitForFinishOrTimeout(timeoutInSecs))
    {
        LOG_err << "test_utils(downloadFile): waitForFinishOrTimeout timeout expired";
        return std::nullopt;
    }
    return err;
}

std::unique_ptr<MegaNode> uploadFile(MegaApi* megaApi,
                                     const std::filesystem::path& localPath,
                                     MegaNode* parentNode)
{
    testing::NiceMock<MockMegaTransferListener> mtl{megaApi};
    handle nodeHandle = UNDEF;
    EXPECT_CALL(mtl, onTransferFinish)
        .WillOnce(
            [&mtl, &nodeHandle](MegaApi*, MegaTransfer* transfer, MegaError* error)
            {
                nodeHandle = transfer->getNodeHandle();
                mtl.markAsFinished(error->getErrorCode() == API_OK);
            });
    megaApi->startUpload(localPath.u8string().c_str(),
                         parentNode ? parentNode :
                                      std::unique_ptr<MegaNode>{megaApi->getRootNode()}.get(),
                         nullptr /*fileName*/,
                         MegaApi::INVALID_CUSTOM_MOD_TIME,
                         nullptr /*appData*/,
                         false /*isSourceTemporary*/,
                         false /*startFirst*/,
                         nullptr /*cancelToken*/,
                         &mtl);
    EXPECT_TRUE(mtl.waitForFinishOrTimeout(MAX_TIMEOUT)) << "Error uploading file: " << localPath;
    if (nodeHandle == UNDEF)
        return nullptr;
    return std::unique_ptr<MegaNode>(megaApi->getNodeByHandle(nodeHandle));
}

std::unique_ptr<MegaNode> uploadFile(MegaApi* megaApi, LocalTempFile&& file, MegaNode* parentNode)
{
    return uploadFile(megaApi, file.getPath(), parentNode);
}

handle createPasswordNode(MegaApi* megaApi,
                          const std::string& name,
                          const MegaNode::PasswordNodeData* data,
                          const handle parentNodeHandle)
{
    NiceMock<MockRequestListener> rl;
    handle newPwdNodeHandle{UNDEF};
    rl.setErrorExpectations(API_OK,
                            _,
                            MegaRequest::TYPE_CREATE_PASSWORD_NODE,
                            [&newPwdNodeHandle](const MegaRequest& req)
                            {
                                newPwdNodeHandle = req.getNodeHandle();
                            });
    megaApi->createPasswordNode(name.c_str(), data, parentNodeHandle, &rl);
    EXPECT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT))
        << "Password node not properly generated. Name: " << name;
    return newPwdNodeHandle;
}

handle createCreditCardNode(::mega::MegaApi* megaApi,
                            const std::string& name,
                            const ::mega::MegaNode::CreditCardNodeData* data,
                            const ::mega::handle parentNodeHandle)
{
    NiceMock<MockRequestListener> rl;
    handle newPwdNodeHandle{UNDEF};
    rl.setErrorExpectations(API_OK,
                            _,
                            MegaRequest::TYPE_CREATE_PASSWORD_NODE,
                            [&newPwdNodeHandle](const MegaRequest& req)
                            {
                                newPwdNodeHandle = req.getNodeHandle();
                            });
    megaApi->createCreditCardNode(name.c_str(), data, parentNodeHandle, &rl);
    EXPECT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT))
        << "CreditCard node not properly generated. Name: " << name;
    return newPwdNodeHandle;
}
}
