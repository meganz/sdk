/**
 * @file BenchReportWriter.h
 * @brief JSON bench-report emitter (one file per test_integration PID).
 *
 * Part of the `bench_framework` reusable module gated by `MEGA_BENCH_FRAMEWORK_ENABLED`.
 * Mirrors the gfx-test artifact pattern (`gfxworker_test_integration_*.log` emitted from
 * `tools/gfxworker/tests/integration/main.cpp:19`). Emits bench cells into a dedicated
 * sub-directory under the per-process directory
 * (`<TestFS::GetProcessFolder()>/bench_reports/` — typically
 * `$HOME/mega_tests/pid_<PID>/bench_reports/` on POSIX,
 * `c:\tmp\mega_tests\pid_<PID>\bench_reports\` on Windows), suitable for CI-side
 * archival via Jenkinsfile.
 *
 * Two artifacts are produced (fu7-18 G1):
 * - `bench_report_<PID>.jsonl` — JSON Lines stream, one cell per line, appended on
 *   every `recordCell()` call and flushed-to-disk immediately so partial runs that
 *   are killed mid-test still leave usable data behind. Requires `setReportDir()` to
 *   have been called once at startup.
 * - `bench_report_<PID>.json` — consolidated array, only written at explicit
 *   `flush()` (typically test tear-down). Kept for backward-compat with any
 *   post-process tooling that consumes the consolidated form.
 *
 * Schema (versioned via `schema_version` field) — see BENCHMARKS.md `## Bench-report JSON`.
 *
 * Thread-safety: `setReportDir`, `recordCell`, and `flush` are mutex-guarded.
 */
#pragma once

#include "BenchProcessStats.h"
#include "BenchSummary.h"
#include "BenchTransferTimings.h"

#include <cstdint>
#include <string>

namespace mega::bench
{

struct BenchReportCell
{
    std::string name;
    Direction direction = Direction::Upload;
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

    // Set the report directory for per-iter JSONL flush. Called once during test
    // framework setup (typically from main.cpp after the process folder is created).
    // The dedicated `bench_reports/` sub-directory under `reportDir` is created on
    // first use. If `setReportDir()` is not called, `recordCell()` still updates
    // internal storage and `flush()` continues to work, but no JSONL stream is
    // produced — the only artifact will be the consolidated JSON emitted from
    // `flush()` at tear-down.
    void setReportDir(const std::string& reportDir);

    // Record one bench cell's results. When `setReportDir()` was called earlier,
    // the cell is also appended (as a single-line JSON object) to
    // `<reportDir>/bench_reports/bench_report_<PID>.jsonl`; the file is opened,
    // flushed, and closed each invocation so the line is durable on disk even if
    // the process is killed mid-test.
    void recordCell(const BenchReportCell& cell);

    // Write the accumulated cells to `<reportDir>/bench_reports/bench_report_<PID>.json`.
    // Idempotent — can be called multiple times during a session to checkpoint partial
    // results. Returns absolute path written, or empty string on failure / no cells /
    // not enabled.
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
