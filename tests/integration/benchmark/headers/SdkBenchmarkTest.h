/**
 * @file SdkBenchmarkTest.h
 * @brief Test fixture for upload/transfer benchmark cells.
 *
 * Lightweight subclass of SdkTest so that TEST_F(SdkBenchmarkTest, X) groups
 * the benchmark cells separately in gtest output, matching how SdkWsUploadTest
 * groups the WS-upload tests. The 4 cells delegate to the runner functions
 * exposed in benchmark/BenchmarkRunners.h; test bodies live in
 * benchmark/SdkBenchmarkTest.cpp and the runner/helper cluster lives in
 * benchmark/BenchmarkRunners.cpp.
 */

#pragma once

#include "SdkTest_test.h"

class SdkBenchmarkTest : public SdkTest
{
};
