/**
 * @file WsUscCommand.h
 * @brief Test helper for issuing the `usc` (USC size-class) command and parsing the response.
 *
 * Extracted from SdkTest_test.cpp anonymous-namespace helpers (CommandUscForTest +
 * fetchUscSizeClasses). The Command-derived class lives in the SDK's mega::Command
 * hierarchy; we keep both the class and the free function in mega::test::wsupload
 * so consumers can call fetchUscSizeClasses(...) directly. Implementations live in
 * WsUscCommand.cpp to avoid pulling SDK-internal headers (command.h, json.h) into
 * every consumer TU.
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
