/**
 * @file
 * @brief File aimed to contain utilities for integration tests where objects from megaapi.h are
 * required. For examples, a function to wait for a sync state to change.
 *
 * These utilities extend the ones defined in the more general level for the tests
 * (sdk_test_utils.h) so the namespace is extended (sdk_test).
 */

#ifndef INCLUDE_INTEGRATION_INTEGRATION_TEST_UTILS_H_
#define INCLUDE_INTEGRATION_INTEGRATION_TEST_UTILS_H_

#include "mega/types.h"
#include "megaapi.h"

#include <memory>
#include <optional>

namespace sdk_test
{

#ifdef ENABLE_SYNC

/**
 * @brief Waits for the sync state to be set to a given value and with a given error during a
 * certain amount of time (30 seconds).
 *
 * @param megaApi The api from where to get the sync object
 * @param remoteNode The root remote node the sync is tracking
 * @param runState The expected run state to match
 * @param err The expected error code to match
 * @return std::unique_ptr<MegaSync> If the sync matches de expected state within that time, the
 * function returns the sync object. Otherwise nullptr.
 */
std::unique_ptr<::mega::MegaSync> waitForSyncState(::mega::MegaApi* megaApi,
                                                   ::mega::MegaNode* remoteNode,
                                                   ::mega::MegaSync::SyncRunningState runState,
                                                   ::mega::MegaSync::Error err);

/**
 * @brief Overloaded implementation where the sync is obtained by the backup id instead of by the
 * remote root node.
 */
std::unique_ptr<::mega::MegaSync> waitForSyncState(::mega::MegaApi* megaApi,
                                                   ::mega::handle backupID,
                                                   ::mega::MegaSync::SyncRunningState runState,
                                                   ::mega::MegaSync::Error err);

/**
 * @brief Synchronously start a TWO_WAY sync between the given local path and the remote node with
 * the given handle.
 *
 * It will also wait until the new sync is in RUNSTATE_RUNNING state.
 *
 * @param megaApi The api to request the sync creation
 * @param localRootPath The local root to sync
 * @param remoteRootHandle The handle of the remote node to sync
 * @return The backupId of the new sync.
 */
::mega::handle syncFolder(::mega::MegaApi* megaApi,
                          const std::string& localRootPath,
                          const ::mega::MegaHandle remoteRootHandle);

/**
 * @brief Synchronously removes the sync with the given backupId
 *
 * @return true if the operation succeed, false otherwise
 */
bool removeSync(::mega::MegaApi* megaApi, const ::mega::handle backupID);

/**
 * @brief Synchronously change the running state of the sync with the given backupId
 *
 * @return true if the operation succeed, false otherwise
 */
bool setSyncRunState(::mega::MegaApi* megaApi,
                     const ::mega::handle backupID,
                     const ::mega::MegaSync::SyncRunningState state);

/**
 * @brief Synchronously resume the sync with the given backupId
 *
 * @return true if the operation succeed, false otherwise
 */
bool resumeSync(::mega::MegaApi* megaApi, const ::mega::handle backupID);

/**
 * @brief Synchronously suspend the sync with the given backupId
 *
 * @return true if the operation succeed, false otherwise
 */
bool suspendSync(::mega::MegaApi* megaApi, const ::mega::handle backupID);

/**
 * @brief Synchronously disable the sync with the given backupId
 *
 * @return true if the operation succeed, false otherwise
 */
bool disableSync(::mega::MegaApi* megaApi, const ::mega::handle backupID);

#endif

/**
 * @brief Get a vector with the names of the nodes that are children of the node with the given
 * handle.
 *
 * If any of the operations to get the nodes fails, a nullopt is returned.
 */
std::optional<std::vector<std::string>>
    getCloudFirstChildrenNames(::mega::MegaApi* megaApi, const ::mega::MegaHandle nodeHandle);
}

#endif // INCLUDE_INTEGRATION_INTEGRATION_TEST_UTILS_H_
