/**
 * @file WsUscCommand.h
 * @brief Test helper for issuing the `usc` (USC size-class) command and parsing the response.
 *
 * Implementation routes through the production mega::CommandUSCForWsUpload parser
 * (declared in mega/commands_ws.h) — no test-side reimplementation. Implementation
 * lives in WsUscCommand.cpp to keep SDK-internal includes out of consumer TUs.
 */

#pragma once

#include "megaapi.h"

#include <cstdint>
#include <vector>

namespace mega
{
class MegaApi;
}

namespace mega::test::wsupload
{

// Synchronously issues the `usc` command via MegaApi's underlying MegaClient and
// returns the per-size-class max-size values. Returns false on timeout, server
// error, or empty result.
//
// Default `timeoutSeconds` (60) matches SdkTest_test.h's defaultTimeout constant.
bool fetchUscSizeClasses(::mega::MegaApi& api,
                         std::vector<int64_t>& maxSizes,
                         int timeoutSeconds = 60);

} // namespace mega::test::wsupload
