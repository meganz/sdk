/**
 * @file BenchProcessStats.h
 * @brief RSS + CPU sampling via getrusage for benchmark cells.
 *
 * Part of the `bench_framework` reusable module gated by `MEGA_BENCH_FRAMEWORK_ENABLED`.
 * Mirrors the per-bench-cell RSS+CPU sampling previously added inline to
 * `SdkTest_test.cpp`. Provides a portable POSIX `getrusage(RUSAGE_SELF)` wrapper.
 * Windows callers receive zeros (not supported here; bench cells run on Linux CI).
 */
#pragma once

#include <cstdint>

namespace mega::bench
{

struct BenchProcessStats
{
    std::int64_t rssMaxKb = 0;
    std::int64_t userCpuMs = 0;
    std::int64_t sysCpuMs = 0;
    bool sampled = false;
};

// Captures the current process-level resource usage. Returns zero-initialised
// struct with `sampled == false` on platforms without getrusage support.
BenchProcessStats sampleProcessStats();

// Returns (after - before) component-wise. If either side is unsampled, returns
// zero-initialised. Negative rss deltas can happen as the kernel rebalances; the
// caller should treat them as informational, not asserts.
BenchProcessStats deltaProcessStats(const BenchProcessStats& before,
                                    const BenchProcessStats& after);

} // namespace mega::bench
