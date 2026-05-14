/**
 * @file SdkWsUploadTest.cpp
 * @brief WS-upload integration tests fixture-class wiring.
 *
 * The SdkWsUploadTest class (declared in SdkWsUploadTest.h) is currently
 * realized as a thin SdkTest subclass so that TEST_F(SdkWsUploadTest, X) tests
 * group separately in gtest output. The test bodies themselves live in
 * tests/integration/SdkTest_test.cpp for now — their helper dependencies
 * (second_timer, fetchUscSizeClasses, CommandUscForTest, etc.) form a tightly
 * coupled cluster in that file's anonymous namespace. Future work (a separate
 * follow-up) can complete the physical TU extraction once those helpers
 * are extracted to shared headers as well; this TU stays minimal so it's a
 * clean target.
 *
 * fu7-7 G3 partial: the rename SdkTest.SdkWsUploadX → SdkWsUploadTest.X
 * happens in place inside SdkTest_test.cpp, with the fixture-class declaration
 * supplied by this module.
 */

#include "wsupload/SdkWsUploadTest.h"

// Test bodies live in tests/integration/SdkTest_test.cpp (fu7-7 G3 partial).
