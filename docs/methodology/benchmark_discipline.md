# Benchmark discipline

The benchmark surface for SDK-5360 is the `SdkBenchmarkTest.*` family
plus a number of `SdkWsUploadTest.*` cells (T1, B9, IP, Resume,
RepeatedPauseResume, MultiplePauseResume) that double as throughput
gates. The discipline below is what makes the numbers comparable across
fu7-N sessions, OSes, and against `origin/develop`.

## Build

The bench binary needs the bench framework enabled at CMake time:

```bash
cmake --preset dev-unix-wsupload-benchOn
cmake --build ../build-sdk-dev-unix-wsupload-benchOn -j16 --target=test_integration
```

Other OS presets (post-fu7-17):

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

Schema (`schema_version: 2` since fu7-10):

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

## Throttle-storm caveat

The production gfs270n* storage hosts run an event=6 throttle regime
that varies during the day. Single-iteration bench numbers can swing
by ~30-50%. Always:

- Run n≥3, ideally n=5+ for statistical comparison.
- Use the **top-2 mean** of n=3 (or top-3 mean of n=5) as the
  comparison statistic.
- Quote the median + IQR (not just the mean) when reporting.
- For fu7-N vs develop comparison, run BOTH binaries in alternating
  iterations on the same day (HR31), not in separate sessions.

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

## Multi-session bench (HR31)

A multi-session bench produces n≥3 paired (fu7-N, develop) measurements
on the same hardware on the same day. Procedure:

1. Build BOTH binaries (`build-sdk-dev-unix-wsupload-benchOn` and
   `build-sdk-develop-dev-unix-wsupload-benchOn`).
2. Run alternating cells: fu7-N iter 1, develop iter 1, fu7-N iter 2, ...
3. Collect both `bench_report_*.json` files.
4. Mann-Whitney U for significance (HR45 + Goal 5 stats).

## Cross-OS scope (post-fu7-17)

Linux is the primary bench platform but macOS and Windows now have
their own presets. Use the Jenkins `--bench` flag in an MR comment to
fire all three:

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
  (`API_ETOOMANY`). See [feedback_tests_sequential.md] memory.
- Don't quote single-iteration throughput as a fix outcome — the
  storm noise will eat the signal.
