/**
 * @file SdkBenchmarkTest.h
 * @brief Test fixture for upload/transfer benchmark cells.
 *
 * Lightweight subclass of SdkTest so that TEST_F(SdkBenchmarkTest, X) groups
 * the benchmark cells separately in gtest output, matching how SdkWsUploadTest
 * groups the WS-upload tests (fu7-7 G3). Test bodies live in
 * tests/integration/SdkTest_test.cpp pending full TU extraction (their helper
 * dependencies — bench_framework BenchSession/BenchSummary, plus
 * file-scope helpers in SdkTest_test.cpp's anonymous namespace — form a
 * tightly-coupled cluster, deferred along with the WS-upload extraction).
 */

#pragma once

#include "SdkTest_test.h"

class SdkBenchmarkTest : public SdkTest
{
};
