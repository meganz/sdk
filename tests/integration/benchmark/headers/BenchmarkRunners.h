/**
 * @file BenchmarkRunners.h
 * @brief Public interface of the SdkBenchmarkTest cell runners.
 *
 * The 4 SdkBenchmarkTest cells (ManySmallUploads / 1kSmallUploads /
 * SingleLargeUpload / LargePlusManySmall) delegate to these 3 runner
 * functions. The cluster of bench helpers (BenchDistribution, BenchTimingSummary,
 * BenchProcessStatsSample, BenchPutnodesTimingRecorder, file-staging helpers,
 * etc.) lives in BenchmarkRunners.cpp as implementation detail.
 *
 * Header-light by design: the test bodies only need the 3 runner signatures.
 */

#pragma once

#include <cstddef>

class SdkTest;

namespace mega::test::benchmark
{

// Drives the small-uploads bench cell: uploads `fileCount` random 1 MiB files
// into a fresh remote folder and logs a greppable `summaryTag` line.
void runSmallUploadsBenchmark(SdkTest& test,
                              std::size_t fileCount,
                              const char* testName,
                              const char* summaryTag);

// Drives the single-large-upload bench cell (10 GiB).
void runSingleLargeUploadBenchmark(SdkTest& test);

// Drives the QA-tester reproduction cell: one 4 MiB file (the exact file the
// QA tester uploaded). Honours the MEGA_NET_MAXUPLOAD_KBPS env var (kilobits/s,
// matching the iOS Network Link Conditioner) to cap upload bandwidth for the
// duration of the transfer, so poor-network behaviour can be reproduced under
// scripts/ci/netem_profile.sh.
void runQaExactSingleFileBenchmark(SdkTest& test);

// Drives the large + many-small mixed bench cell (10 GiB + 500 * 1 MiB).
void runLargePlusManySmallBenchmark(SdkTest& test);

// Drives the small-file-burst cell: N (default 20, env MEGA_BENCH_BURST_COUNT)
// 256 KiB files uploaded STRICTLY SEQUENTIALLY (file i+1 starts only after i
// completes). Measures warm-connection reuse / handshake amortisation that the
// PARALLEL ManySmall cell cannot. Honours MEGA_NET_MAXUPLOAD_KBPS (as QaExact)
// and MEGA_BENCH_UPLOAD_CONNECTIONS. Emits a greppable [BenchSmallFileBurst]
// summary line with coldFileMs (first file) vs warmMedianMs (files 2..N).
void runSmallFileBurstBenchmark(SdkTest& test);

// Drives the QA real-media reproduction: uploads a fixed on-disk dataset of real
// media files (dir from MEGA_BENCH_UPLOAD_SOURCE_DIR) so thumbnail/preview fa
// generation is reproduced. Honours MEGA_NET_MAXUPLOAD_KBPS + MEGA_BENCH_UPLOAD_CONNECTIONS.
void runQaMixedUploadBenchmark(SdkTest& test);

// Drives the QA nested-folder reproduction (SDK-5360): ONE MegaApi::startUpload()
// of the whole directory tree at MEGA_BENCH_UPLOAD_SOURCE_DIR (skipped when unset),
// so the recursive folder controller submits every subtransfer in one go.
// Measurement cell — axes, env knobs and gating are in BENCHMARKS.md.
void runQaNestedFolderUploadBenchmark(SdkTest& test);

} // namespace mega::test::benchmark
