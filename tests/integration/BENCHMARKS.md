# Integration benchmarks and stress tests

Quick reference for integration tests used to validate WS upload engine, sync engine, and transfer scheduling changes. Extend this file as new scenarios appear.

## Running

```bash
cd <build_dir>/tests/integration            # e.g. ../build-sdk-dev-unix-wsupload
source ./environment2.txt                   # mandatory: sets MEGA_EMAIL etc.
./test_integration --CI --COUT --USERAGENT:<tag> --gtest_filter=<pattern>
```

Test logs land at `/home/vmga/mega_tests/pid_<PID>/test_integration.log`. To find the latest run:

```bash
ls -td /home/vmga/mega_tests/pid_* | head -1
```

**Never `cat` a multi-GB log.** Use `grep`, `head`, `tail`, `wc -l` only.

## Core regression targets

Tests whose failure/flake usually indicates a real bug, not infra noise.

| Test | What it exercises | Typical wall-clock | Workload |
| --- | --- | --- | --- |
| `SyncTest.BasicSync_MassNotifyFromLocalFolderTree` | Sync-upload drain of a massive local tree; primary WS-upload livelock regression test | ~6–10 min when passing; hangs indefinitely on livelock | 16 000 × ~17 B files |
| `SdkWsUploadTest.RepeatedPauseResumeMixedPools` | Cross-pool fairness under pause/resume; sensitive to `nextEligible` rotation semantics and preflight retry timing | ~2 min | 4 files, two USC size classes |
| `SdkWsUploadTest.ActivePoolUsesParallelConnections` | Pool/connection fan-out | ~12–15 s | 1 × 12 MB |

## Throughput benchmarks

Use for A/B comparison when changing scheduling, preflight, or transfer path.

| Test | Workload | Typical wall-clock (pre-Fix-B baseline) |
| --- | --- | --- |
| `SdkTest.SdkTestMultipleUploads` | 2 parallel: 160 MB + 900 KB | ~13–15 s |
| `SdkTest.SdkTestMultipleUploadsExpanded` | One file per USC size class (up to 160 MB each) | ~16–19 s |
| `SdkTest.SdkTestUploads` | 5 × 160 MB sequential | ~22–28 s |
| `SdkTest.SdkTestBenchmarkManySmallUploads` | 500 × 1 MiB sequentially queued | TBD (first-run benchmark) |

Per-transfer metrics are logged by `TransferTracker` completion — grep the log for `upload time`, `KB/s`, `mean speed`.

## Broader sweeps

Use when investigating side effects beyond the immediate fix.

| Filter | Tests | Budget |
| --- | --- | --- |
| `SdkWsUploadTest.*` | 30 | ~15–20 min |
| `SyncTest.*Notify*:SyncTest.*Scan*:SdkWsUploadTest.*` | 31 | ~20–25 min |
| `SdkTest.*Sync*:SdkTestSyncRootOperations.*:SdkTestSyncUploadThrottling.*:SdkTestSyncLocalRootChange.*:SdkTestBackupSyncLocalRootChange.*:SdkTestSyncPrevalidation.*:SyncFingerprintCollisionTest.*` | 59 | ~45–90 min |
| `SyncTest.*` | 78 | 60–120+ min; some individual tests exceed 25 min |

## Known-bad / expected failures

Do **not** attribute these to your fix without evidence.

- `SdkTest.SyncImage` — media-related, fails in current test env, pre-existing. Treat as expected.
- `SdkWsUploadTest.OverquotaDuringTransfer` — reliably SKIPs ("Could not inject overquota into active WS upload path in this environment").

## Test-infrastructure quirks

- **`--gtest_repeat=N` on tests that create many syncs/nodes** (notably `SyncTest.BasicSync_MassNotifyFromLocalFolderTree` and several `BasicSync_Move*`) can hit `API_ETOOMANY (-13)` after iter 1 because the test account retains state across iterations. The failure appears at sync setup (`Sync_test.cpp:3243` `e == -13 vs API_OK`), not in the upload code. Expected; not a fix regression.
- **Long `test_integration` processes (60+ min, 13+ large SyncTest tests in one invocation)** have been seen to SIGSEGV during later tests' setup (e.g. `BasicSync_ResumeSyncFromSessionAfterClashingLocalAddRemoteDelete`). Cause unclear; likely accumulated process state. Mitigate by running tests in smaller groups.
- **`SdkTestMultipleUploads` iter-3 slowdown under back-to-back runs** is proportional to server-side residual files; not a livelock.

## Metrics worth grepping for upload-path changes

```bash
PID=$(ls -td /home/vmga/mega_tests/pid_* | head -1)
log="$PID/test_integration.log"

# Preflight dynamics (Fix-B-relevant)
grep -c 'preflightStart(f->transfer()) -> return false' "$log"   # timeouts
grep -c 'prepareUploadForWs.*return true' "$log"                 # successes
grep -c 'prepareUploadForWs.*preflight failed' "$log"            # real failures (expect 0)

# Scheduler cost
grep -c 'nextEligible' "$log"                                    # scan volume
grep 'fileList.size=' "$log" | tail -5                           # queue depth peak/tail

# Pool brackets (sanity on applyRefreshedUrls)
grep 'mMinFileSize=' "$log" | head -5

# Transfer rates
grep -E 'mean speed|KB/s|upload time' "$log" | head -20
```

## Diagnostic protocol when a test hangs

1. `ps -ef | grep test_integration` — confirm it is still alive.
2. `pstack <pid> > /tmp/pstack.txt` — fast non-destructive snapshot.
3. `gcore -o /tmp/core <pid>` — persist state.
4. `gdb -batch -p <pid> -ex "thread apply all bt 50"` — full thread dump.
5. Look for contention on `UploadEngine::Impl::uploadMutex` and `MegaClient::clientMutex`. A thread blocked inside `std::__future_base::_State_baseV2::wait_for` from `wsPrepareUploadForWsSync` is the classic Fix-B-era livelock signature.

## Reference commands / prior-session artifacts

- Single-test timing: `time ./test_integration --gtest_filter=<test>`.
- Sequential multi-test driver (pattern): see `/tmp/phase5_validate.sh` retained from the 2026-04-22 session — it writes per-phase stdout + pid dirs + summary under `/tmp/phase5/`.
- Example baseline / before-after reports for the WS upload engine: `~/investigationTests/SDK-5360_serialization_new/SyncTest/followup/` — `{01_baseline, 04_fixB_results, 07_isolation_matrix, 08_fixB_prime_results}.md`.

## Bench-report JSON (fu7-5 G6)

The `MEGA_BENCH_FRAMEWORK_ENABLED` CMake option defaults to **OFF** — see the
"Benchmark procedure" section below for the local build that has it ON.
When the binary is built with `-DMEGA_BENCH_FRAMEWORK_ENABLED=ON`, each
`SdkBenchmark*` test emits a JSON cell into
`/home/vmga/mega_tests/pid_<PID>/bench_report_<PID>.json` summarising that
cell's measurements. Schema (version 1):

```json
{
  "schema_version": 1,
  "session_pid": 12345,
  "cells": [
    {
      "name": "SdkBenchmarkTest.SingleLargeUpload",
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

The file is intended to be archived by Jenkins as a CI artifact (glob `pid_*/bench_report_*.json`).
Use this in preference to grepping `[WsUploadStats]` from `test_integration.log` for ≥5 % regression
detection — the JSON file is small, version-locked, and diffable across runs.

The framework module (`tests/integration/bench_framework/`) provides reusable helpers:
- `mega::bench::BenchSession` — wall-clock lifecycle wrapper.
- `mega::bench::BenchProcessStats` — `getrusage(RUSAGE_SELF)` sample + delta.
- `mega::bench::BenchTransferTiming` — per-transfer phase milestones.
- `mega::bench::BenchSummary` — min/max/median/p95 distribution + aggregate kBps.
- `mega::bench::BenchReportWriter` — singleton JSON emitter.

Future `SdkBenchmarkDownload*` cells can be added with ≤20 LOC of integration by reusing
these helpers; see `tests/integration/benchmark/` for the existing `SdkBenchmark*` patterns.

## Extending this file

When a new scenario or test is used for benchmarking:

- Add it to the relevant section (core / throughput / sweep).
- Include workload description, typical wall-clock, and why it exercises what it does.
- If it has known-bad failure modes or infra quirks, document them alongside.

Keep the file short and scannable — the full investigative context should stay in session reports under `~/investigationTests/`, not here.

## Benchmark procedure

Bench cells emit `pid_<PID>/bench_report_<PID>.json` only when the binary is
compiled with `-DMEGA_BENCH_FRAMEWORK_ENABLED=ON`. The default
`dev-unix-wsupload` (and `dev-unix`) preset has the flag **OFF** and is used
for regression-test runs only.

### Build the ON binary

```bash
cmake --preset dev-unix-wsupload-benchOn
cmake --build ../build-sdk-dev-unix-wsupload-benchOn -j16 --target=test_integration
```

`dev-unix-wsupload-benchOn` inherits `dev` + `unix` and sets
`MEGA_BENCH_FRAMEWORK_ENABLED=ON`. Verify:

```bash
grep MEGA_BENCH_FRAMEWORK_ENABLED ../build-sdk-dev-unix-wsupload-benchOn/CMakeCache.txt
# expected: MEGA_BENCH_FRAMEWORK_ENABLED:BOOL=ON
```

### Run a benchmark cell

Use the wrapper `tests/integration/run_bench.sh` — it picks the ON binary,
checks `SdkBenchmark*` cells are registered, sources `environment2.txt`,
and prints the resulting JSON path.

```bash
bash tests/integration/run_bench.sh 'SdkBenchmarkTest.SingleLargeUpload' 1
# … run output …
# Latest bench_report:
#   /home/vmga/mega_tests/pid_<PID>/bench_report_<PID>.json
```

Override the build dir with `BENCH_BUILD_DIR=...` if needed.

### Local layout under `pid_<PID>/`

After a bench run, `pid_<PID>/` should contain:

```
pid_<PID>/
├── bench_report_<PID>.json   # BenchReportWriter output (ON binary only)
├── bench_staging/            # speculative staging dir; empty for cells that don't stage local files (see BenchmarkRunners)
├── test_integration.log      # gtest stdout + line summaries
└── mega.gfxworker.*.log      # gfx worker logs
```

Notes:

- The empty `bench_staging/` directory is a deliberate, eager creation by
  `tests/integration/bench_framework/BenchmarkRunners.cpp` and can be left
  as-is.
- `downfile<N> (M).txt` files inside `pid_<PID>/` (e.g. `downfile1.txt`,
  `downfile1 (1).txt`, …) come from
  `COLLISION_RESOLUTION_NEW_WITH_N` semantics in the SDK download API when
  the same target name is used across iterations. This is the intended
  rename, not a cleanup bug. If a test wants per-iter cleanup, it must
  remove the file between runs explicitly.

### Cross-version comparison from the JSON

The JSON is per-PID and additive (one cell entry per gtest cell). Compare
across versions with `jq`:

```bash
# Compare SingleLargeUpload aggregate_kbps across two PIDs
jq '.cells[] | select(.name=="SdkBenchmarkTest.SingleLargeUpload") |
    {pid: input_filename, kbps: .aggregate_kbps, duration_ms: .duration_ms,
     rss_delta_kb: .rss_delta_kb}' \
   /home/vmga/mega_tests/pid_AAAA/bench_report_AAAA.json \
   /home/vmga/mega_tests/pid_BBBB/bench_report_BBBB.json
```

The schema is version-locked (`schema_version` field); the same `jq` recipe
keeps working across runs.

### Jenkins-side artifacts

MR builds run a separate `${BUILD_DIR}_bench` configured with
`-DMEGA_BENCH_FRAMEWORK_ENABLED=ON` (see `jenkinsfile/Jenkinsfile_MR_linux_cmake`).
After the bench stage the pipeline csplits `bench_sweep.log` into per-cell
logs and packs them into `bench_logs_<BUILD_ID>.tar.gz`; both that tarball
and the raw `bench_report_*.json` are archived via the Jenkins
`archiveArtifacts` glob.

To reproduce the per-cell csplit locally for a sweep that emitted a single
log:

```bash
csplit -k -f bench_logs_local_ -b '%03d.log' "${PID_DIR}/test_integration.log" \
    '/^\[ RUN      \]/' '{*}' 2>/dev/null
tar -czf bench_logs_local.tar.gz bench_logs_local_*.log
```

