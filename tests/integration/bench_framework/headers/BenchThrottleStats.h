/**
 * @file BenchThrottleStats.h
 * @brief Per-iter snapshot of WS-upload throttle counters consumed by BenchReportWriter.
 *
 * Part of the `bench_framework` reusable module gated by `MEGA_BENCH_FRAMEWORK_ENABLED`.
 *
 * The struct is a *snapshot* type with plain integer fields — the live
 * counters live as `std::atomic<std::int64_t>` inside `WsPool` (Release-safe,
 * incremented with `memory_order_relaxed` on the WS hot path). Each iter the
 * bench-runner calls `UploadEngine::getAndResetThrottleStats()` which
 * atomically swaps each counter to zero and returns a `BenchThrottleStats`
 * value; that value is then stored on the `BenchReportCell` and emitted to
 * `throttle_summary_<PID>.jsonl` alongside the existing bench artifact.
 *
 * Schema is documented at `tests/integration/BENCHMARKS.md` (`## Throttle
 * summary JSON`).
 */
#pragma once

#include <cstdint>

namespace mega::bench
{

struct BenchThrottleStats
{
    // Count of server-side throttle frames (WsApiServerEvent::Throttle, event=6)
    // received across all pools/conns during this iter.
    std::int64_t event6Count = 0;
    // Sum of pause-duration parameters (ms) carried by those event=6 frames.
    std::int64_t event6TotalMs = 0;
    // Count of WsPool::pauseSending invocations during this iter. In current
    // code one event=6 maps 1:1 to one pauseSending call; the two counters are
    // tracked independently so any future divergence is observable.
    std::int64_t pauseCount = 0;
    // Sum of pause durations (ms) applied through WsPool::pauseSending.
    std::int64_t pauseTotalMs = 0;
};

} // namespace mega::bench
