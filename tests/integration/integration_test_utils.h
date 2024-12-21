/**
 * @file
 * @brief File aimed to contain utilities for integration tests where objects from megaapi.h are
 * required. For examples, a function to wait for a sync state to change.
 *
 * These utilities extend the ones defined in the more general level for the tests
 * (sdk_test_utils.h) so the namespace is extended (sdk_test).
 */

#include "mega/types.h"
#include "megaapi.h"

#include <memory>

namespace sdk_test
{

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

}
