/**
 * @file megaapi_impl_sync.cpp
 * @brief Private implementation of the intermediate layer for sync-related functionality.
 */

#ifdef ENABLE_SYNC

#include "megaapi_impl.h"

namespace
{
using namespace mega;

/**
 * @brief Helper to populate the related request fields corresponding to the
 * MegaRequestSyncFolderParams.
 *
 * @see MegaApiImpl::syncFolder()
 * @see MegaApiImpl::prevalidateSyncFolder()
 */
void populateRequest_syncFolder(MegaRequestPrivate& request, MegaRequestSyncFolderParams&& params)
{
    request.setNodeHandle(params.megaHandle);

    if (!params.localFolder.empty())
        request.setFile(params.localFolder.c_str());

    // Use provided name if available, or if it's a backup, even an empty name
    if (!params.name.empty() || params.type == SyncConfig::TYPE_BACKUP)
    {
        request.setName(params.name.c_str());
    }
    else if (!params.localFolder.empty())
    {
        request.setName(params.localFolder.c_str()); // fallback to localFolder
    }

    request.setParamType(params.type);

    if (!params.driveRootIfExternal.empty())
        request.setLink(params.driveRootIfExternal.c_str());
}

/**
 * @brief Helper to build an initial sync configuration with the request fields populated after
 * populateRequest_syncFolder().
 *
 * @param client A reference to the MegaClient used for remote node checking purposes.
 * @return A pair with an error (API_OK if success) and the new SyncConfig (empty object if there is
 * an error).
 *
 * @see populateRequest_syncFolder()
 * @see MegaApiImpl::syncFolder()
 * @see MegaApiImpl::prevalidateSyncFolder()
 */
std::pair<error, SyncConfig> prepareSyncConfig(const MegaRequestPrivate& request,
                                               MegaClient& client)
{
    const std::string localPath{request.getFile() ? request.getFile() : ""};
    const std::string name{request.getName() ? request.getName() : ""};
    const std::string drivePath{request.getLink() ? request.getLink() : ""};

    return buildSyncConfig(static_cast<SyncConfig::Type>(request.getParamType()),
                           localPath,
                           name,
                           drivePath,
                           request.getNodeHandle(),
                           client);
}
} // namespace

namespace mega
{
// Public methods
void MegaApiImpl::syncFolder(MegaRequestSyncFolderParams&& params,
                             MegaRequestListener* const listener)
{
    auto completion = [this](const auto request, auto&& config, auto&& revertOnError)
    {
        completeRequest_syncFolder_AddSync(request,
                                           std::forward<decltype(config)>(config),
                                           std::forward<decltype(revertOnError)>(revertOnError));
    };

    addRequest_syncFolder(MegaRequest::TYPE_ADD_SYNC,
                          std::move(params),
                          listener,
                          std::move(completion));
}

void MegaApiImpl::prevalidateSyncFolder(MegaRequestSyncFolderParams&& params,
                                        MegaRequestListener* const listener)
{
    auto completion = [this](auto&&... params)
    {
        completeRequest_syncFolder_PrevalidateAddSync(std::forward<decltype(params)>(params)...);
    };

    addRequest_syncFolder(MegaRequest::TYPE_ADD_SYNC_PREVALIDATION,
                          std::move(params),
                          listener,
                          std::move(completion));
}

// Private/internal methods
void MegaApiImpl::addRequest_syncFolder(const int megaRequestType,
                                        MegaRequestSyncFolderParams&& params,
                                        MegaRequestListener* const listener,
                                        SyncFolderRequestCompletion&& completion)
{
    auto* const request = new MegaRequestPrivate(megaRequestType, listener);

    populateRequest_syncFolder(*request, std::move(params));

    request->performRequest = [this, request, completion = std::move(completion)]() mutable -> error
    {
        return performRequest_syncFolder(request, std::move(completion));
    };

    requestQueue.push(request);
    waiter->notify();
}

error MegaApiImpl::performRequest_syncFolder(MegaRequestPrivate* const request,
                                             SyncFolderRequestCompletion&& completion)
{
    auto [err, syncConfig] = prepareSyncConfig(*request, *client);
    if (err != API_OK)
    {
        return err;
    }

    if (syncConfig.getType() != SyncConfig::TYPE_BACKUP)
    {
        completion(request, std::move(syncConfig), nullptr);
        return API_OK;
    }

    auto preparebackupCompletion =
        [this, request, completion = std::move(completion)](const auto err,
                                                            auto&& backupConfig,
                                                            auto&& revertOnError)
    {
        if (err)
        {
            fireOnRequestFinish(request, std::make_unique<MegaErrorPrivate>(err));
            return;
        }

        assert(revertOnError &&
               "No revertOnError action if there is a failure during backup set up!");
        request->setNodeHandle(backupConfig.mRemoteNode.as8byte());
        completion(request,
                   std::forward<decltype(backupConfig)>(backupConfig),
                   std::forward<decltype(revertOnError)>(revertOnError));
    };
    client->preparebackup(std::move(syncConfig), std::move(preparebackupCompletion));

    return API_OK;
}

void MegaApiImpl::completeRequest_syncFolder_AddSync(MegaRequestPrivate* const request,
                                                     SyncConfig&& syncConfig,
                                                     MegaClient::UndoFunction&& revertOnError)
{
    auto completion =
        [this, request, revertOnError = std::move(revertOnError)](auto err,
                                                                  const auto syncError,
                                                                  const auto backupId)
    {
        request->setNumDetails(syncError);

        if (client->syncs.hasSyncConfigByBackupId(backupId))
        {
            request->setParentHandle(backupId);
            fireOnRequestFinish(request, std::make_unique<MegaErrorPrivate>(err, syncError));
            return;
        }

        if (!err)
        {
            LOG_debug << "[MegaApiImpl::completeRequest_syncFolder_AddSync] Correcting error "
                         "to API_ENOENT for sync add";
            err = API_ENOENT;
        }

        if (!revertOnError)
        {
            fireOnRequestFinish(request, std::make_unique<MegaErrorPrivate>(err, syncError));
            return;
        }

        revertOnError(
            [this, request, err, syncError]()
            {
                fireOnRequestFinish(request, std::make_unique<MegaErrorPrivate>(err, syncError));
            });
    };

    client->addsync(std::move(syncConfig), std::move(completion), "", basePath);
}

void MegaApiImpl::completeRequest_syncFolder_PrevalidateAddSync(
    MegaRequestPrivate* const request,
    SyncConfig&& syncConfig,
    MegaClient::UndoFunction&& revertForBackup)
{
    const auto syncErrorInfo = client->checkSyncConfig(syncConfig);
    const auto err = std::get<0>(syncErrorInfo);
    const auto syncError = std::get<1>(syncErrorInfo);

    request->setNumDetails(syncError);

    if (syncConfig.getType() != SyncConfig::TYPE_BACKUP)
    {
        fireOnRequestFinish(request, std::make_unique<MegaErrorPrivate>(err, syncError));
        return;
    }

    if (!revertForBackup)
    {
        LOG_err << "[MegaApiImpl::prevalidateAddSyncByRequest] expected a handler to revert the "
                   "backup node and it is null";
        assert(false && "expected a handler to revert the backup node and it is null!");

        fireOnRequestFinish(request, std::make_unique<MegaErrorPrivate>(err, syncError));
        return;
    }

    revertForBackup(
        [this, request, err, syncError]()
        {
            request->setNodeHandle(MegaHandle());
            fireOnRequestFinish(request, std::make_unique<MegaErrorPrivate>(err, syncError));
        });
}

} // namespace mega

#endif // ENABLE_SYNC
