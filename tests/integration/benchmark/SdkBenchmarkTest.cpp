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

#include "benchmark/headers/SdkBenchmarkTest.h"

#include "benchmark/headers/BenchmarkRunners.h"

namespace mega::test::benchmark {

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

/**
 * @brief Benchmark / QA reproduction: one 4 MiB file (the exact file the QA
 * tester uploaded) is sent into a fresh remote folder. Honours the
 * MEGA_NET_MAXUPLOAD_KBPS env var (kilobits/s) to cap upload bandwidth, so the
 * poor-network upload can be reproduced under scripts/ci/netem_profile.sh. The
 * test emits a greppable [BenchQaExactSingleFile] summary line.
 */
TEST_F(SdkBenchmarkTest, QaExactSingleFile)
{
    runQaExactSingleFileBenchmark(*this);
}

TEST_F(SdkBenchmarkTest, LargePlusManySmall)
{
    runLargePlusManySmallBenchmark(*this);
}

/**
 * @brief Benchmark: N (default 20, env MEGA_BENCH_BURST_COUNT) 256 KiB files
 * uploaded STRICTLY SEQUENTIALLY into a fresh remote folder (file i+1 starts
 * only after i completes). Measures warm-connection reuse / handshake
 * amortisation that the PARALLEL ManySmall cell cannot. Honours
 * MEGA_NET_MAXUPLOAD_KBPS (kilobits/s) so the burst can run under
 * scripts/ci/netem_profile.sh. Emits a greppable [BenchSmallFileBurst] summary
 * line with coldFileMs vs warmMedianMs.
 */
TEST_F(SdkBenchmarkTest, SmallFileBurst)
{
    runSmallFileBurstBenchmark(*this);
}

} // namespace mega::test::benchmark
