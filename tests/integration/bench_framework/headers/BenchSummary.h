/**
 * @file BenchSummary.h
 * @brief Distribution + aggregate-kBps summary helpers for benchmark cells.
 *
 * Part of the `bench_framework` reusable module gated by `MEGA_BENCH_FRAMEWORK_ENABLED`.
 * Provides min/max/median/p95 calculation over a sample vector, plus aggregate-kBps
 * derivation from a vector of `BenchTransferTiming`. Designed to replace the inline
 * `BenchDistribution` + `appendBenchTimingFields` helpers in `SdkTest_test.cpp`.
 */
#pragma once

#include "BenchTransferTimings.h"

#include <cstdint>
#include <vector>

namespace mega::bench
{

struct BenchSummaryDistribution
{
    double min = 0;
    double max = 0;
    double mean = 0;
    double median = 0;
    double p95 = 0;
    std::size_t n = 0;
};

// In-place sort and compute distribution stats. Empty input returns zero-initialised.
BenchSummaryDistribution computeDistribution(std::vector<double>& samplesInOut);

// Aggregate kBps across all timings (total bytes / sum-of-totalMs * 1000 / 1024).
// Returns 0 if vector is empty or sum-of-totalMs is zero.
double computeAggregateKbps(const std::vector<BenchTransferTiming>& timings);

// Convenience: extract per-transfer totalMs values for distribution input.
std::vector<double> totalMsSamples(const std::vector<BenchTransferTiming>& timings);

} // namespace mega::bench
