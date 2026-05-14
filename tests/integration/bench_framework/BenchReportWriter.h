/**
 * @file BenchReportWriter.h
 * @brief JSON bench-report emitter (one file per test_integration PID).
 *
 * Part of the `bench_framework` reusable module gated by `MEGA_BENCH_FRAMEWORK_ENABLED`.
 * Mirrors the gfx-test artifact pattern (`gfxworker_test_integration_*.log` emitted from
 * `tools/gfxworker/tests/integration/main.cpp:19`). Emits `bench_report_<PID>.json` into
 * the per-process directory (`<TestFS::GetProcessFolder()>/bench_report_<PID>.json` —
 * typically `$HOME/mega_tests/pid_<PID>/` on POSIX, `c:\tmp\mega_tests\pid_<PID>\` on
 * Windows), suitable for CI-side archival via Jenkinsfile.
 *
 * Schema (versioned via `schema_version` field) — see BENCHMARKS.md `## Bench-report JSON`.
 *
 * Thread-safety: `recordCell` and `flush` are mutex-guarded.
 */
#pragma once

#include "BenchProcessStats.h"
#include "BenchSummary.h"

#include <cstdint>
#include <string>

namespace mega::bench
{

struct BenchReportCell
{
    std::string name;
    std::int64_t fileSizeMib = 0;
    unsigned connections = 0;
    std::int64_t durationMs = 0;
    double aggregateKbps = 0;
    std::int64_t firstByteMs = 0;
    std::int64_t lastByteMs = 0;
    BenchProcessStats rssCpuDelta;
    BenchSummaryDistribution chunkMsDist;
};

class BenchReportWriter
{
public:
    static BenchReportWriter& instance();

    // Record one bench cell's results.
    void recordCell(const BenchReportCell& cell);

    // Write the accumulated cells to `<reportDir>/bench_report_<PID>.json`. Idempotent —
    // can be called multiple times during a session to checkpoint partial results.
    // Returns absolute path written, or empty string on failure / no cells / not enabled.
    std::string flush(const std::string& reportDir);

    // Test-only: clear accumulated cells (for use between independent suite runs).
    void reset();

    std::size_t cellCount() const;

private:
    BenchReportWriter() = default;
    BenchReportWriter(const BenchReportWriter&) = delete;
    BenchReportWriter& operator=(const BenchReportWriter&) = delete;
};

} // namespace mega::bench
