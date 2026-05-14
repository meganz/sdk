/**
 * @file SdkWsUploadTest.h
 * @brief Test fixture for WS-upload integration tests.
 *
 * Lightweight subclass of SdkTest so that TEST_F(SdkWsUploadTest, X) groups
 * the WS-upload-related tests separately in gtest output. The bodies live in
 * tests/integration/SdkTest_test.cpp (fu7-7 G3 partial — full TU extraction
 * is deferred because the bodies share a tightly-coupled cluster of
 * anonymous-namespace helpers with the rest of SdkTest_test.cpp).
 */

#pragma once

#include "SdkTest_test.h"

class SdkWsUploadTest : public SdkTest
{
};
