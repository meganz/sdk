/**
 * @file mega/transfer/ws/ws_quota_types.h
 * @brief Shared value types for the websocket-upload quota subsystem.
 *
 *        Home of WsTfsGroupBalances — the parsed shape of the "tfs" quota reply
 *        (one group per entry: writable bytes + the folder handles sharing that
 *        pool). Defined here rather than in commands_ws.h so the alias is visible
 *        to the always-compiled MegaTestHooks::onWsTfsResult field in testhooks.h
 *        without dragging the MEGA_USE_WSUPLOAD-gated command header into that
 *        always-compiled translation unit. Consumed by commands_ws.h,
 *        transfer/ws/ws_quota.h and testhooks.h. Intentionally NOT gated on
 *        MEGA_USE_WSUPLOAD: testhooks.h references the type unconditionally.
 *
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
 *
 * @copyright Simplified (2-clause) BSD License.
 */

#ifndef MEGA_TRANSFER_WS_WS_QUOTA_TYPES_H
#define MEGA_TRANSFER_WS_WS_QUOTA_TYPES_H 1

#include "mega/types.h"

#include <utility>
#include <vector>

namespace mega
{

// One quota group from the "tfs" reply: writable bytes plus the folder handles
// that share that quota pool. The canonical alias for the parsed reply shape,
// referenced by CommandTfsForWsUpload, ws::UploadQuotaManager and the
// onWsTfsResult test hook.
using WsTfsGroupBalances = std::vector<std::pair<m_off_t, std::vector<NodeHandle>>>;

} // namespace mega

#endif // MEGA_TRANSFER_WS_WS_QUOTA_TYPES_H
