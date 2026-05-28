/**
 * @file WsUploadHelpers.h
 * @brief Aggregate header that pulls in the WS-upload test helper cluster.
 *
 * `SdkWsUploadTest.cpp` is the SOLE consumer (post-D2 refactor) of the full
 * cluster: it needs ALL 12 helper headers, so the umbrella avoids 11 lines
 * of duplicated #include text in the test TU. The individual headers remain
 * includable directly; downstream callers may import only what they need.
 *
 * Per `Goal4_refactors/d2_design.md` this delivers +8 LOC net (umbrella
 * adds 17 LOC; SdkWsUploadTest.cpp drops 10). The FollowupRequest §4
 * estimate (−400 LOC) didn't match the actual file landscape (2 .cpp
 * TUs in `wsupload/`, not 8) — see d2_design.md §2 for the deviation
 * documentation.
 */

#pragma once

#include "wsupload/headers/ScopedUploadSpeedLimit.h"
#include "wsupload/headers/SdkWsUploadTest.h"
#include "wsupload/headers/SecondTimer.h"
#include "wsupload/headers/TransferTempErrorTracker.h"
#include "wsupload/headers/WsChunkSendOverquotaCapture.h"
#include "wsupload/headers/WsOneShotHelper.h"
#include "wsupload/headers/WsUploadDebugHelpers.h"
#include "wsupload/headers/WsUploadHookGate.h"
#include "wsupload/headers/WsUploadRetryTracker.h"
#include "wsupload/headers/WsUploadTestHelpers.h"
#include "wsupload/headers/WsUploadTransitionCapture.h"
#include "wsupload/headers/WsUscCommand.h"
