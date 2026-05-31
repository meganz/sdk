# Benchmark discipline

The benchmark surface is the `SdkBenchmarkTest.*` family plus a number
of `SdkWsUploadTest.*` cells (T1, B9, IP, Resume, RepeatedPauseResume,
MultiplePauseResume) that double as throughput gates. The discipline
below is what makes the numbers comparable across sessions, OSes, and
against `origin/develop`.

## Build

The bench binary needs the bench framework enabled at CMake time:

```bash
cmake --preset dev-unix-wsupload-benchOn
cmake --build ../build-sdk-dev-unix-wsupload-benchOn -j16 --target=test_integration
```

Other OS presets:

- `dev-macos-wsupload-benchOn`
- `dev-windows-wsupload-benchOn`

Verify:

```bash
grep MEGA_BENCH_FRAMEWORK_ENABLED ../build-sdk-dev-unix-wsupload-benchOn/CMakeCache.txt
# expected: MEGA_BENCH_FRAMEWORK_ENABLED:BOOL=ON
```

The default `dev-unix-wsupload` preset has the flag **OFF**. Running
`SdkBenchmarkTest.*` on the OFF binary still works but emits no JSON
artifact — bench numbers come from grepping `[BenchSingleLargeUpload]`
log lines, which is brittle and version-dependent. Always prefer the
ON binary for bench A/B comparisons.

## JSON artifact

When the binary is `MEGA_BENCH_FRAMEWORK_ENABLED=ON`, each
`SdkBenchmarkTest.*` cell appends a JSON entry to:

```
${HOME}/mega_tests/pid_<PID>/bench_report_<PID>.json
```

Schema (current):

```json
{
  "schema_version": 2,
  "session_pid": 12345,
  "cells": [
    {
      "name": "SdkBenchmarkTest.SingleLargeUpload",
      "direction": "upload",
      "file_size_mib": 1024,
      "connections": 8,
      "duration_ms": 87234,
      "aggregate_kbps": 12345,
      "first_byte_ms": 234,
      "last_byte_ms": 87100,
      "rss_delta_kb": 42000,
      "user_cpu_ms": 12300,
      "sys_cpu_ms": 4500,
      "chunk_ms_min": 10,
      "chunk_ms_max": 250,
      "chunk_ms_mean": 47,
      "chunk_ms_median": 42,
      "chunk_ms_p95": 95,
      "chunk_n": 1024
    }
  ]
}
```

The Jenkins MR build archives `bench_report_*.json` and
`bench_logs_<BUILD_ID>.tar.gz` (per-cell logs split via `csplit`).

## How to run

Use the wrapper `tests/integration/run_bench.sh`:

```bash
bash tests/integration/run_bench.sh 'SdkBenchmarkTest.SingleLargeUpload' 3
```

The wrapper picks the ON binary, sources `environment2.txt`, passes
`--USERAGENT:JenkinsCanSpam-SDK`, and prints the resulting JSON path
at the end. Override the build dir with `BENCH_BUILD_DIR=...`.

Use this rather than calling `./test_integration` directly so the
USERAGENT and `--CI --COUT` flags are consistent across sessions.

## USERAGENT requirement

The `scopedToPro` helper (used by all `SdkBenchmarkTest.*` and
`SdkWsUploadTest.*` cells) issues an admin `setAccountLevel` call. The
production API gates this on the USERAGENT header. The whitelisted
value is `JenkinsCanSpam-SDK`.

A non-whitelisted USERAGENT (any ad-hoc tag) causes:

```
SdkTest_test.cpp:3163: Failure
Expected equality of these values: result -11, API_OK 0
Couldn't restore account level: -11
```

i.e. `API_EACCESS` at setup. If this fires, FIRST check the
USERAGENT; the symptom is NOT account drift. See
[feedback_scopedtopro_account_requirement.md] memory.

## `rss_delta_kb` topology dependence — HR45 + HR56 corollary

`rss_delta_kb` is computed as `getrusage(RUSAGE_SELF).ru_maxrss(after) -
ru_maxrss(before)` (see `tests/integration/bench_framework/BenchProcessStats.cpp`).
On Linux, `ru_maxrss` is the **process-lifetime peak RSS** — it is
monotonically non-decreasing within a process and resets on every new
process.

**Practical consequence**: `rss_delta_kb` is only comparable across two
bench runs that share the SAME process topology.

- **Single process × `--gtest_repeat=N`** (HR56 canonical topology):
  iter 1 reports the cold-start peak (typically tens of MB for the
  upload subsystem); iters 2..N report ~0 KB as the peak is already
  past. The MEDIAN across N iters is dominated by the trailing zeros,
  not by the cold-start cost. This is the topology that `BENCHMARKS.md`
  expects.
- **N independent processes × `--gtest_repeat=1` each**: every iter
  is a fresh process and pays the full cold-start cost; the per-iter
  series is flat at the cold-start peak. The median across N
  independent processes is ~`peak`, NOT comparable to the single-
  process median.

Comparing the two topologies produces a multi-hundred-percent "leak"
that is entirely an artifact of `ru_maxrss`'s scope; fu7-20 Session 2
spent significant wall-clock chasing one such artifact (see
`empirical_3variant_verdict.md` under `RSSLeakBisect/` in the fu7-20
investigation tree, and the HR56 codification).

**Rules**:
1. The canonical bench topology is single-process `--gtest_repeat=N`
   (HR56). Drivers that fan out across N processes per cell are NOT
   permitted to populate `rss_delta_kb` for cross-session comparison.
2. When comparing RSS across sessions / variants / commits, verify
   topology identity FIRST (look at `session_pid` cardinality in the
   JSONLs being compared). The aggregator emits a warning when the
   candidate's per-iter PID cardinality differs from any baseline.
3. If you need cross-process RSS comparability, sample `rss_max_kb`
   (high-water for that process; same cold-start semantics) or
   `/proc/self/statm` resident RSS (current, not peak — comparable
   across processes) at a deterministic point in each iter, not
   `ru_maxrss(after) - ru_maxrss(before)`.

## Throttle-storm caveat

The production gfs270n* storage hosts run an event=6 throttle regime
that varies during the day. Single-iteration bench numbers can swing
by ~30-50%. Always:

- Run n≥3, ideally n=5+ for statistical comparison.
- Use the **top-2 mean** of n=3 (or top-3 mean of n=5) as the
  comparison statistic.
- Quote the median + IQR (not just the mean) when reporting.
- For candidate-vs-baseline comparison, run BOTH binaries in alternating
  iterations on the same day, not in separate sessions.

The endpoint is throttle-frequency-sensitive but not
throughput-sensitive in steady state — see
[project_sdk5360_throttle_endpoint.md] memory.

## Comparison: scripts/ci/compare_bench.py

```bash
python3 scripts/ci/compare_bench.py \
  ${HOME}/mega_tests/pid_AAAA/bench_report_AAAA.json \
  ${HOME}/mega_tests/pid_BBBB/bench_report_BBBB.json
```

The script diffs aggregate_kbps, duration_ms, rss_delta_kb, user_cpu_ms
between two sessions. Output is a table; flagged deltas at ≥5% have an
asterisk.

For TSAN log archives use `scripts/ci/compare_tsan.sh`.

## Multi-session bench (paired alternating-iter)

A multi-session bench produces n≥3 paired (candidate, baseline)
measurements on the same hardware on the same day. Procedure:

1. Build BOTH binaries (`build-sdk-dev-unix-wsupload-benchOn` and the
   baseline equivalent under a separate build dir).
2. Run alternating cells: candidate iter 1, baseline iter 1,
   candidate iter 2, ...
3. Collect both `bench_report_*.json` files.
4. Mann-Whitney U for significance (see project's stats convention).

## Cross-OS scope

Linux is the primary bench platform but macOS and Windows have their
own presets. Use the Jenkins `--bench` flag in an MR comment to fire
all three:

```
trigger compilation --bench --gtest_filter=SdkBenchmarkTest.SingleLargeUpload
```

See [docs/ci/CI_TESTING_GUIDE.md](../ci/CI_TESTING_GUIDE.md) for the
full trigger-phrase catalogue.

## What NOT to do

- Don't compare bench numbers from different builds with different
  `MEGA_BENCH_FRAMEWORK_ENABLED` settings; the JSON schema differs.
- Don't parallelize integration tests — bench numbers depend on
  stable wall-clock; the shared test account is rate-limited
  (`API_ETOOMANY`).
- Don't quote single-iteration throughput as a fix outcome — the
  storm noise will eat the signal.

## Bench-proof report standard

Every bench-proof report should include full distribution stats per
cell × axis:

- min, p25, median, mean, p75, p95, max
- σ (population standard deviation)
- σ/μ (coefficient of variation)

Use `scripts/ci/aggregate_bench.py` to compute and emit these stats
plus Mann-Whitney U significance + Cliff's d effect size in one pass.
Do not leave placeholder values (e.g. `TBD`) in any committed
bench-proof markdown. If a stat cannot be computed (e.g. n<5 for the
chosen baseline), state the reason inline ("n=3, IQR not computed")
rather than using a placeholder.
