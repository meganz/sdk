/**
 * @file WsTfsCommand.h
 * @brief Test helper for issuing the `tfs` (per-folder quota balances) command
 *        and parsing the response.
 *
 * Implementation routes through the production mega::CommandTfsForWsUpload parser
 * (declared in mega/commands_ws.h) — no test-side reimplementation. Implementation
 * lives in WsTfsCommand.cpp to keep the MEGA_USE_WSUPLOAD-gated command header out
 * of consumer TUs (mirrors WsUscCommand.h/.cpp).
 *
 * Unlike WsUscCommand, the `tfs` reply carries an application error alongside the
 * parsed groups (unknown folders are silently omitted, but a malformed/denied
 * reply surfaces an Error), so the helper returns BOTH the Error and the parsed
 * WsTfsGroupBalances. The bool return distinguishes a dispatch/timeout failure
 * (client thread never set the promise) from a real server reply.
 */

#pragma once

#include "mega/transfer/ws/ws_quota_types.h" // WsTfsGroupBalances (not MEGA_USE_WSUPLOAD-gated)
#include "mega/types.h" // NodeHandle, Error
#include "megaapi.h"

#include <chrono>
#include <vector>

namespace mega
{
class MegaApi;
}

namespace mega::test::wsupload
{

// Synchronously issues the `tfs` command for `folders` via MegaApi's underlying
// MegaClient (production CommandTfsForWsUpload) and returns the parsed per-pool
// balances plus the application Error.
//
// Returns true iff the client thread executed the command and set the promise
// within `timeout` (i.e. a real reply was received — inspect `err`/`out`).
// Returns false on dispatch/teardown race or timeout; `err` is then set to
// API_EINTERNAL and `out` is left untouched.
bool fetchTfsGroups(::mega::MegaApi& api,
                    const std::vector<::mega::NodeHandle>& folders,
                    ::mega::WsTfsGroupBalances& out,
                    ::mega::Error& err,
                    std::chrono::seconds timeout);

} // namespace mega::test::wsupload
