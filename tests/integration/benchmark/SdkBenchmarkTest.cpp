/**
 * @file SdkBenchmarkTest.cpp
 * @brief Benchmark-cell TEST_F bodies for SdkBenchmarkTest.
 *
 * The 4 cells delegate to the runner functions defined in BenchmarkRunners.cpp.
 * The cells live here so that the gtest fixture grouping
 * (`SdkBenchmarkTest.*`) is owned by the benchmark module and gtest output
 * cleanly separates the bench cells from the rest of the SDK integration
 * suite.
 */

#include "benchmark/SdkBenchmarkTest.h"

#include "benchmark/BenchmarkRunners.h"

using ::mega::test::benchmark::runLargePlusManySmallBenchmark;
using ::mega::test::benchmark::runSingleLargeUploadBenchmark;
using ::mega::test::benchmark::runSmallUploadsBenchmark;

/**
 * @brief Benchmark: 500 random files of 1 MiB each are uploaded into one
 * freshly created remote folder. The test emits a greppable
 * [BenchManySmallUploads] summary line and, on debug wsupload builds, a
 * [WsUploadStats] counter line.
 */
TEST_F(SdkBenchmarkTest, ManySmallUploads)
{
    runSmallUploadsBenchmark(*this,
                             500,
                             "SdkTestBenchmarkManySmallUploads",
                             "[BenchManySmallUploads]");
}

TEST_F(SdkBenchmarkTest, 1kSmallUploads)
{
    runSmallUploadsBenchmark(*this,
                             1000,
                             "SdkTestBenchmark1kSmallUploads",
                             "[Bench1kSmallUploads]");
}

TEST_F(SdkBenchmarkTest, SingleLargeUpload)
{
    runSingleLargeUploadBenchmark(*this);
}

TEST_F(SdkBenchmarkTest, LargePlusManySmall)
{
    runLargePlusManySmallBenchmark(*this);
}
