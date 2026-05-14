/**
 * @file SdkBenchmarkTest.cpp
 * @brief Benchmark fixture-class wiring (fu7-7 G4 partial).
 *
 * The SdkBenchmarkTest class (declared in SdkBenchmarkTest.h) is currently
 * realized as a thin SdkTest subclass so that TEST_F(SdkBenchmarkTest, X)
 * tests group separately in gtest output. The test bodies themselves live
 * in tests/integration/SdkTest_test.cpp for now — their helper dependencies
 * form a tightly coupled cluster with the rest of SdkTest_test.cpp. Future
 * work (a separate follow-up) can complete the physical TU extraction once
 * those helpers are pulled out as shared headers as well.
 *
 * G6 recordCell wiring (Phase E) inserts BenchReportWriter::recordCell(...)
 * inside the 4 bench runners' bodies at their current home in
 * SdkTest_test.cpp.
 */

#include "benchmark/SdkBenchmarkTest.h"

// Bench bodies live in tests/integration/SdkTest_test.cpp (fu7-7 G4 partial).
