/**
 * @file SdkWsUploadTest.h
 * @brief Test fixture for WS-upload integration tests.
 *
 * Lightweight subclass of SdkTest so that TEST_F(SdkWsUploadTest, X) groups
 * the WS-upload-related tests separately in gtest output. The 30 test
 * bodies live in tests/integration/wsupload/SdkWsUploadTest.cpp alongside
 * the WS-upload helper headers (WsUploadRetryTracker, WsUploadTransferSnapshot,
 * SecondTimer, TransferTempErrorTracker, etc.).
 */

#pragma once

#include "mega/testhooks.h"
#include "SdkTest_test.h"

#include <mutex>

class SdkWsUploadTest : public SdkTest
{
protected:
    void TearDown() override
    {
        // A leaked WsUploadServerEventHook rule silently rewrites/drops server events in the NEXT
        // cell (a leaked Modify(1->5) retires its pools; a leaked secondaryDrop(5) hides genuine
        // Distress). Repair FIRST so a failing cell cannot chain-poison the rest of the batch,
        // then report against the cell that actually leaked.
        const bool leaked = ::mega::globalMegaTestHooks.wsUploadServerEventHook.isArmed();
        {
            std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
            ::mega::globalMegaTestHooks.wsUploadServerEventHook.reset();
        }
        SdkTest::TearDown();
        EXPECT_FALSE(leaked) << "WsUploadServerEventHook left armed at TearDown -- add a "
                                "makeScopedDestructor that calls reset() on every exit path "
                                "(pattern: SdkWsUploadTest.cpp GoodputGate cleanupHooks)";
    }
};
