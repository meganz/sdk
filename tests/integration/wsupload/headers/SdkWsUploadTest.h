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

#include "SdkTest_test.h"

class SdkWsUploadTest : public SdkTest
{
};
