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
 * Two artifacts are produced:
 * - `bench_report_<PID>.jsonl` — JSON Lines stream, one cell per line, appended on
 *   every `recordCell()` call and flushed-to-disk immediately so partial runs that
 *   are killed mid-test still leave usable data behind. Requires `setReportDir()` to
 *   have been called once at startup.
 * - `bench_report_<PID>.json` — consolidated array, only written at explicit
 *   `flush()` (typically test tear-down). Kept for backward-compat with any
 *   post-process tooling that consumes the consolidated form.
 *
 * Schema (versioned via `schema_version` field) — see BENCHMARKS.md `## Bench-report JSON`.
 * Version 3 (SDK-5360 followup9.1) added `first_progress_after_stage_ms`,
 * `first_finish_after_stage_ms`, `preflight_peak` and `action_queue_peak`, and
 * introduced `schema_version` on the per-line JSONL form (a line WITHOUT the field
 * predates this change and carries none of the four new keys).
 *
 * Thread-safety: `setReportDir`, `recordCell`, and `flush` are mutex-guarded.
 */
#pragma once

#include "BenchProcessStats.h"
#include "BenchSummary.h"
#include "BenchThrottleStats.h"
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
    std::int64_t iter = 0; // 0-based iter index within the run.
    std::int64_t durationMs = 0;
    double aggregateKbps = 0;
    std::int64_t firstByteMs = 0;
    std::int64_t lastByteMs = 0;
    BenchProcessStats rssCpuDelta;
    // Iter-end peak RSS (getrusage ru_maxrss high-water at cell end), in KB.
    // Distinct from rssCpuDelta.rssMaxKb, which despite its field name carries
    // the (end - start) RSS *delta*. This absolute peak is the HR54 /
    // aggregate_bench.py `rss_max_kb` axis (the v2 RSS-reduction premise) and
    // removes the need for any log-scraping post-step. 0 if unsampled (Windows).
    std::int64_t rssMaxKb = 0;
    BenchSummaryDistribution chunkMsDist;
    // Folder-transfer latency axes (SDK-5360 QaNestedFolderUpload). Milliseconds
    // from the folder transfer's STAGE_TRANSFERRING_FILES notification to,
    // respectively, the first byte of progress reported by ANY file subtransfer
    // and the first subtransfer to finish with API_OK. They quantify the
    // app-visible "nothing is happening" window of a recursive folder upload,
    // which the per-file cells cannot see because they never build a subtransfer
    // backlog. Convention: 0 = the cell does not measure this axis (every cell
    // other than QaNestedFolderUpload); -1 = the cell ran but the callback never
    // arrived (premise broken — the runner also fails an EXPECT).
    std::int64_t firstProgressAfterStageMs = 0;
    std::int64_t firstFinishAfterStageMs = 0;
    // Process-lifetime high-water marks read from
    // `UploadEngine::WsUploadStatsForTesting` at cell end: outstanding
    // speculative preflight requests and queued client-thread actions. 0 on
    // hooks-off builds and on cells that do not sample them.
    std::int64_t preflightPeak = 0;
    std::int64_t actionQueuePeak = 0;
    // Per-iter throttle snapshot drained from
    // `UploadEngine::getAndResetBenchThrottleStats()` before each
    // `recordCell()` so iters are independent.
    BenchThrottleStats throttleStats;
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
