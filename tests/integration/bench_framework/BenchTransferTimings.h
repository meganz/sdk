/**
 * @file BenchTransferTimings.h
 * @brief Per-transfer timing record used by benchmark cells.
 *
 * Part of the `bench_framework` reusable module gated by `MEGA_BENCH_FRAMEWORK_ENABLED`.
 * Records the four phase milestones (apiStart, firstByte, lastByte, apiEnd) plus byte
 * count, in a uniform shape that future benchmark cells can share.
 *
 * Designed to be a value-type container — collect a `std::vector<BenchTransferTiming>`
 * during the bench run, then pass it to BenchSummary for aggregation.
 */
#pragma once

#include <cstdint>

namespace mega::bench
{

struct BenchTransferTiming
{
    std::int64_t apiStartMs = 0;
    std::int64_t firstByteMs = 0;
    std::int64_t lastByteMs = 0;
    std::int64_t apiEndMs = 0;
    std::int64_t bytes = 0;

    std::int64_t totalMs() const noexcept
    {
        return apiEndMs - apiStartMs;
    }

    std::int64_t firstByteLatencyMs() const noexcept
    {
        return firstByteMs - apiStartMs;
    }

    std::int64_t streamMs() const noexcept
    {
        return lastByteMs - firstByteMs;
    }
};

} // namespace mega::bench
