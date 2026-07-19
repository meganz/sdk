# Integration benchmarks and stress tests

Quick reference for integration tests used to validate WS upload engine, sync engine, and transfer scheduling changes. Extend this file as new scenarios appear.

## Running

```bash
cd <build_dir>/tests/integration            # e.g. ../build-sdk-dev-unix-wsupload
source ./environment2.txt                   # mandatory: sets MEGA_EMAIL etc.
./test_integration --CI --COUT --USERAGENT:<tag> --gtest_filter=<pattern>
```

Test logs land at `${HOME}/mega_tests/pid_<PID>/test_integration.log`. To find the latest run:

```bash
ls -td ${HOME}/mega_tests/pid_* | head -1
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
PID=$(ls -td ${HOME}/mega_tests/pid_* | head -1)
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

## Bench-report JSON

The `MEGA_BENCH_FRAMEWORK_ENABLED` CMake option defaults to **OFF** — see the
"Benchmark procedure" section below for the local build that has it ON.
When the binary is built with `-DMEGA_BENCH_FRAMEWORK_ENABLED=ON`, each
`SdkBenchmark*` test emits a JSON cell into the dedicated
`bench_reports/` sub-directory of the per-process folder, separate from
`test_integration.log` so partial-run artifacts survive truncation:

```
${HOME}/mega_tests/pid_<PID>/
├── test_integration.log
└── bench_reports/
    ├── bench_report_<PID>.jsonl    # per-iter stream
    └── bench_report_<PID>.json     # consolidated array (final flush)
```

**`bench_report_<PID>.jsonl`** — JSON Lines stream, one cell per line, appended
synchronously on every `recordCell()` call and flushed to disk immediately. Mid-run
safe: if the process is killed (throttle storm, SIGKILL, crash) the lines completed
so far are durable on disk. Consume with `jq -s` or any JSONL reader.

**`bench_report_<PID>.json`** — consolidated array, only written on explicit
`flush()` at test tear-down. Identical cell schema, wrapped in a top-level
`{ "schema_version": 2, "session_pid": <PID>, "cells": [ ... ] }` object. Use
this when post-processing tooling expects a single document.

Each cell (whether a JSONL line or an entry in the consolidated `cells` array)
has the shape:

```json
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
```

Both files are archived by Jenkins as CI artifacts (glob
`pid_*/bench_reports/bench_report_*.{json,jsonl}`). Use them in preference to
grepping `[WsUploadStats]` from `test_integration.log` for ≥5 % regression
detection — they are small, version-locked, and diffable across runs.

The framework module (`tests/integration/bench_framework/`) provides reusable helpers:
- `mega::bench::BenchSession` — wall-clock lifecycle wrapper.
- `mega::bench::BenchProcessStats` — `getrusage(RUSAGE_SELF)` sample + delta.
- `mega::bench::BenchTransferTiming` — per-transfer timing milestones.
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
#   ${HOME}/mega_tests/pid_<PID>/bench_report_<PID>.json
```

Override the build dir with `BENCH_BUILD_DIR=...` if needed.

### Local layout under `pid_<PID>/`

After a bench run, `pid_<PID>/` should contain:

```
pid_<PID>/
├── bench_reports/                 # BenchReportWriter outputs (ON binary only)
│   ├── bench_report_<PID>.jsonl   #   per-recordCell append; mid-run safe
│   └── bench_report_<PID>.json    #   consolidated array, written at tear-down only
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
  remove the file between runs explicitly. (A per-iter `deleteFile()`
  was added before the downloads in `SdkTestTransferStats_test.cpp` so
  this specific cell no longer accumulates `(N)` suffixes — generic
  guidance for other tests still applies.)

### Cross-version comparison from the JSON

The consolidated JSON is per-PID and additive (one cell entry per gtest cell).
Compare across versions with `jq`:

```bash
# Compare SingleLargeUpload aggregate_kbps across two PIDs (consolidated form)
jq '.cells[] | select(.name=="SdkBenchmarkTest.SingleLargeUpload") |
    {pid: input_filename, kbps: .aggregate_kbps, duration_ms: .duration_ms,
     rss_delta_kb: .rss_delta_kb}' \
   ${HOME}/mega_tests/pid_AAAA/bench_reports/bench_report_AAAA.json \
   ${HOME}/mega_tests/pid_BBBB/bench_reports/bench_report_BBBB.json

# Same query against the JSONL stream (partial-run-safe form). `jq -s` slurps
# the JSONL lines into an array; the cell schema is identical.
jq -s 'map(select(.name=="SdkBenchmarkTest.SingleLargeUpload")) |
    map({kbps: .aggregate_kbps, duration_ms: .duration_ms,
         rss_delta_kb: .rss_delta_kb})' \
   ${HOME}/mega_tests/pid_AAAA/bench_reports/bench_report_AAAA.jsonl
```

The schema is version-locked (`schema_version` field on the consolidated form);
the same `jq` recipe keeps working across runs.

### Jenkins-side artifacts

MR builds run a separate `${BUILD_DIR}_bench` configured with
`-DMEGA_BENCH_FRAMEWORK_ENABLED=ON` (see `jenkinsfile/Jenkinsfile_MR_linux_cmake`).
After the bench stage the pipeline csplits `bench_sweep.log` into per-cell
logs and packs them into `bench_logs_<BUILD_ID>.tar.gz`; both that tarball
and the raw `bench_report_*.{json,jsonl}` (from the `pid_*/bench_reports/`
sub-directory) are archived via the Jenkins `archiveArtifacts`
glob.

To reproduce the per-cell csplit locally for a sweep that emitted a single
log:

```bash
csplit -k -f bench_logs_local_ -b '%03d.log' "${PID_DIR}/test_integration.log" \
    '/^\[ RUN      \]/' '{*}' 2>/dev/null
tar -czf bench_logs_local.tar.gz bench_logs_local_*.log
```

## Benchmarking a branch vs develop (standard procedure)

End-to-end, tracked-only procedure for proving a feature branch's upload
performance against `develop`. It needs nothing outside this repository: the
`dev-unix-wsupload-benchOn` build, the four `SdkBenchmarkTest` cells, and
`scripts/ci/aggregate_bench.py`. No local post-processing scripts are required
on the candidate side — the bench JSONL is self-describing (each per-iter line
carries `aggregate_kbps`, `rss_max_kb`, `rss_delta_kb`, `user_cpu_ms`,
`sys_cpu_ms`, and chunk stats).

The four bench cells (all uploads):

| Cell | Workload |
| --- | --- |
| `SdkBenchmarkTest.ManySmallUploads` | 500 × 1 MiB |
| `SdkBenchmarkTest.1kSmallUploads` | 1000 × 1 MiB |
| `SdkBenchmarkTest.SingleLargeUpload` | 1 × large (≈1 GiB) |
| `SdkBenchmarkTest.LargePlusManySmall` | 1 large + many small |

### 1. Build the two binaries

**Candidate (your branch)** — bench framework ON:

```bash
cmake --preset dev-unix-wsupload-benchOn
cmake --build ../build-sdk-dev-unix-wsupload-benchOn -j16 --target=test_integration
grep MEGA_BENCH_FRAMEWORK_ENABLED ../build-sdk-dev-unix-wsupload-benchOn/CMakeCache.txt
# expected: MEGA_BENCH_FRAMEWORK_ENABLED:BOOL=ON
```

**develop comparison** — `develop` has no bench framework and no
`SdkBenchmarkTest.*` cells, so it requires a small measurement overlay (next
subsection).

### How the develop side is benched (measurement overlay)

`develop` ships neither `MEGA_BENCH_FRAMEWORK_ENABLED` nor the `SdkBenchmarkTest`
cells, so nothing on `develop` emits a comparable per-iter artifact. The
reproducible way to get a develop baseline is a throwaway measurement overlay
on a clean `develop` checkout:

1. **The four workloads as `SdkTest.SdkTestBenchmark*` cells.** Port the runner
   bodies from `tests/integration/benchmark/BenchmarkRunners.cpp`
   (`runSmallUploadsBenchmark`, `runSingleLargeUploadBenchmark`,
   `runLargePlusManySmallBenchmark`) into develop's
   `tests/integration/SdkTest_test.cpp` as `TEST_F(SdkTest, SdkTestBenchmark*)`,
   dropping the `MEGA_BENCH_FRAMEWORK_ENABLED`-gated `recordBenchCell()` call
   (develop has no `BenchReportWriter`). Each cell logs one greppable summary
   line per iter with at least `aggregateKBps` and `totalMs`.
2. **Per-cell RSS + CPU.** A `[ProcessStats] suite=… name=… rss_max_kb=…
   user_cpu_ms=… sys_cpu_ms=…` line per test from `getrusage(RUSAGE_SELF)` in
   `SdkTest::TearDown()` (one `getrusage` call) supplies the develop-side peak
   RSS the aggregator's RSS gate compares against.

Build the overlay binary as a normal develop integration build, then run the
four cells as `SdkTest.SdkTestBenchmark*`.

**Projecting the develop log into aggregator-shaped JSONL.** The aggregator
consumes per-iter JSONL with `name`, `aggregate_kbps`, `duration_ms`,
`rss_max_kb`, `user_cpu_ms`, `sys_cpu_ms`. The overlay emits these across two log
lines (the per-iter summary + the per-cell `[ProcessStats]` line) in
`test_integration.log`. Produce one JSONL object per iter by pairing, for each
cell, the iter's summary line with that cell's `[ProcessStats]` line, e.g.:

```jsonl
{"name": "SdkTestBenchmarkSingleLargeUpload", "aggregate_kbps": 123456, "duration_ms": 87234, "rss_max_kb": 410000, "user_cpu_ms": 12300, "sys_cpu_ms": 4500}
```

`aggregate_bench.py`'s `CELL_ALIASES` canonicalises both the candidate names
(`SingleLargeUpload`, `SdkTestBenchmark*`) and these develop names onto the same
short cell key. A short branch-local grep-to-JSONL script is sufficient; it is
the only develop-side glue and is independent of any candidate-side tooling.

> The candidate side needs **no** such extraction — its JSONL already contains
> `rss_max_kb` directly (emitted by `BenchReportWriter`). The overlay +
> extraction is purely the develop-baseline step, kept small and throwaway.

### 2. Run the four cells, candidate then develop, sequentially

Run at **n=15** in a single process via `--gtest_repeat=15` — one process per
binary. Never spawn multiple `test_integration` processes (they share the
production upload throttle bucket, the test account, and the CPU, which
invalidates the comparison), and run candidate then develop **back-to-back, not
concurrently**. Always pass `--USERAGENT:JenkinsCanSpam-SDK` (the bench cells
call `scopedToPro`, whose admin path is whitelisted only for that user-agent).

Candidate (the tracked wrapper sources `environment2.txt`, logs UTC timestamps,
and prints the produced JSONL path):

```bash
bash tests/integration/run_bench.sh \
  'SdkBenchmarkTest.ManySmallUploads:SdkBenchmarkTest.1kSmallUploads:SdkBenchmarkTest.SingleLargeUpload:SdkBenchmarkTest.LargePlusManySmall' \
  15
# -> prints jsonl: ${HOME}/mega_tests/pid_<CAND_PID>/bench_reports/bench_report_<CAND_PID>.jsonl
# (set BENCH_COLLECT_DIR=<dir> to also copy it to a stable timestamped path)
```

develop overlay (ordinary integration run of the overlaid cells):

```bash
cd ../build-sdk-dev-overlay/tests/integration   # your develop-overlay build dir
source ./environment2.txt
./test_integration --CI --COUT --USERAGENT:JenkinsCanSpam-SDK \
  --gtest_filter='SdkTest.SdkTestBenchmark*' --gtest_repeat=15
# then project test_integration.log -> develop_bench_report.jsonl as described above
```

### 3. Aggregate and read the verdict

Write a manifest `baselines.json`:

```json
{
  "candidate": { "label": "my-branch", "jsonl": "/abs/path/bench_report_<CAND_PID>.jsonl" },
  "baselines": [ { "label": "develop", "is_develop": true, "jsonl": "/abs/path/develop_bench_report.jsonl" } ],
  "rss_paired_allowed": ["manysmall", "1ksmall"]
}
```

- `is_develop: true` marks the baseline so the RSS gate is enforced against it.
- `rss_paired_allowed` (optional) — *short* cell keys (`manysmall`, `1ksmall`,
  `slu`, `largeplusmanysmall`) whose peak-RSS axis may be paired-or-worse vs
  develop instead of strictly lower. Use only for the small-file cells (fixed
  connection-pool overhead the percentage exaggerates against their tiny
  baseline); the large-workload cells (`slu`, `largeplusmanysmall`) are where
  the streaming design must pay off and are NOT eligible. Each entry surfaces as
  `PAIRED-OK-WITH-FOLLOWUP` (a documented caveat, not a win).

```bash
scripts/ci/aggregate_bench.py --manifest baselines.json --output-dir bench_proof_out/
```

Read `bench_proof_out/master_summary.md`:

- **§1** — per-cell × baseline × primary-axis matrix. Primary axes:
  `aggregate_kbps` (higher=better), `rss_max_kb` (iter-end peak RSS, lower=better
  — the streaming-upload memory premise), `user_cpu_ms` (lower=better). Each row
  shows Δ%, a Mann-Whitney p-value, an effect band, and a one-line prose verdict.
- **§5** — the three-dimensional gate: **Throughput** (every cell ≥ develop;
  large negative deltas with p ≥ 0.05 are not-significant, not regressions),
  **Coverage** (every primary axis produced a comparison — a missing `rss_max_kb`
  in either JSONL trips this), and **RSS vs develop** (peak `rss_max_kb` strictly
  lower, Δ% < −2% and p < 0.05, unless the cell is on `rss_paired_allowed`).
- The script prints, and the summary ends with, `BENCH_PROOF_PASS` or
  `BENCH_PROOF_FAIL — …`, and exits non-zero on FAIL.

### Throttle-variance caveat

Production upload throttle varies during the day (the same binary can post a 3×
different wall-time window across runs hours apart). Keep it honest: use
**n ≥ 15** per cell, run candidate and develop the **same day, back-to-back**
(never parallel), and rely on the aggregator's Mann-Whitney significance gating
rather than raw medians. The sibling `throttle_summary_<PID>.jsonl` (emitted next
to the bench JSONL) records each iter's `event=6` throttle exposure;
`master_summary.md` §4 rolls it up per binary so you can confirm the two runs saw
comparable throttle pressure.

## Pre-push strict-warning check

Before pushing wsupload-touching changes, run the `dev-unix-strict` preset on
Linux to catch Win/Mac-only warnings (e.g., MSVC `C4244` narrowing, clang
`-Wmismatched-tags` struct-vs-class) that the regular `dev-unix-wsupload`
preset on gcc misses. Configure once per session, then build the changed
target:

```bash
cmake --preset dev-unix-strict
cmake --build ../build-sdk-dev-unix-strict -j8 --target=SDKlib --target=test_integration
```

The preset is a Debug build (which enables `-Werror` via the project's
existing `target_platform_compile_options`) plus `-Wmismatched-tags` and
`-Wsign-conversion`. Sign-conversion is demoted to a warning via
`-Wno-error=sign-conversion` so the ~19 pre-existing narrowings in unrelated
files (`megaapi_impl.{h,cpp}`, `filefingerprint.cpp` — tracked separately)
don't block compilation. Tag-mismatch stays a hard error since the codebase
is otherwise clean on that axis.

After building, grep the log for warnings touching files in your changeset:

```bash
cmake --build ../build-sdk-dev-unix-strict -j8 --target=SDKlib 2>&1 \
    | tee /tmp/strict_build.log | grep -E "warning:|error:" \
    | grep -E "src/transfer/ws/|include/mega/transfer/ws/|tests/integration/wsupload/"
```

The preset is not committed to any CI stage — it's an author-side gate. Its
presence in `CMakePresets.json` is intentional so all checkouts pick it up;
the individual feature commits should NOT enable the strict flags by default.

## Hooks-off compile gate (post-hook-ABI redesign)

The `dev-unix-hooks-off` preset (Release build → defines `NDEBUG` →
disables `MEGASDK_DEBUG_TEST_HOOKS_ENABLED`) verifies that the WS upload
test fixtures still compile after the hook-ABI redesign
(commits `b1cb43b4b2` + `be84f481dd`) when the hook surface is gone.

```bash
cmake --preset dev-unix-hooks-off
cmake --build ../build-sdk-dev-unix-hooks-off -j8 --target=SDKlib --target=test_integration
```

The `MegaTestHooks` struct, the `globalMegaTestHooks` global, the
`WsUploadDebugHelpers.h` free functions, and the `getClientForTesting()` /
`executeOnThreadForTesting()` accessors are all unconditionally compiled
after the redesign. The `DEBUG_TEST_HOOK_*` macros remain gated and
expand to nothing in NDEBUG, so hook installation at test sites is a
runtime no-op. `WSUPLOAD_REQUIRE_TEST_HOOKS()` from
`tests/integration/wsupload/WsUploadHookGate.h` expands to
`GTEST_SKIP() << "..."` in NDEBUG; tests using it run-skip on the
hooks-off binary.

Run this gate any time changes to remaining hook-gated TEST_F
bodies in `SdkWsUploadTest.cpp` touch new TEST_F bodies, to catch
hooks-OFF compile breakage early.

## Gate-v2 QCT-K knobs (SDK-5360 fu8 S8)

The goodput-saturation gate controller (`WsPool::runGoodputGateLocked`) was redesigned in fu8 S8
("QCT-K": sticky-quorum fast-engage, gain-judged climb with knee memory, exponential failed-probe
backoff, halving trim). All knobs are read-once process statics (set them BEFORE process start;
setenv inside a test is a silent no-op).

| Knob | Default | Range | Meaning |
|---|---|---|---|
| `MEGA_WS_GATE_BP_QUORUM_PCT` | 50 | [1,100] | % of open conns backpressured at a ~2 Hz tick for the tick to count quorum-true. 100 ~= the S7 all-conns predicate. |
| `MEGA_WS_GATE_ENGAGE_WINDOWS` | 2 | [1,60] | Consecutive quorum-true windows to engage from base (anti-stampede debounce). |
| `MEGA_WS_GATE_PROBE_WINDOWS` | 2 | [1,10] | Probe measurement horizon (goodput averaged over it; 1 = S7 single-window judging). |
| `MEGA_WS_GATE_RETREAT_MAX_WINDOWS` | 300 | [5,3000] | Exponential failed-probe backoff cap (base 5 windows = the S7 retreat cooldown). |
| `MEGA_WS_GATE_TRIM_WINDOWS` | 10 | [0,255] | Consecutive quorum-false windows per halving trim toward base; 0 = trim off (S7 never-shrink). |
| `MEGA_WS_GATE_CEILING_MULT` | 4 | [0,32] | Effective ceiling = min(ceiling, base x mult) IN-GATE (desktop 8x4=32 unchanged; mobile 3x4=12). 0 = off. GATE=0 jump path unaffected. |
| `MEGA_WS_CONN_TELEMETRY_MS` | 10000 | 0=off | `[WsConnTelemetry]` per-pool conn-trajectory period, emitted on BOTH gate states (K32 arms + guardrail-killed runs stay scorable). |
| `MEGA_WS_TAIL_COMPLETION_TIMEOUT_MS` | 60000 | >0 (ms) | fu8 S9 tail-completion watchdog window: a file with ALL bytes server-confirmed but no completion frame for longer than this (quiescent or no pool: inflight=0, resend empty) is failed for retry (same recovery as the handshake escalation). Per-FILE clock since fu8 S11 (survives pool retirement — the per-pool clock could never accumulate a window ≥ the 20-30s refresh cadence). Gated by `MEGA_WS_ACKSTALL_WATCHDOG` AND `MEGA_WS_TAILCOMPLETION_WATCHDOG`. The integration tests self-configure 5000 via debug hook (HR58); this env knob still wins when set. |
| `MEGA_WS_TAILCOMPLETION_WATCHDOG` | 1 (on) | "0"=off | fu8 S11: INDEPENDENT enable for the tail-completion watchdog (previously only the shared `MEGA_WS_ACKSTALL_WATCHDOG` gate existed, so the tail net could not be A/B'd or killed alone). ANDed with the shared gate — only ever narrows; `MEGA_WS_ACKSTALL_WATCHDOG=0` / `MEGA_WS_LOSS_RECOVERY=0` still disable BOTH watchdogs byte-identically. |
| `MEGA_TEST_DATA_URL` | Artifactory base URL | URL | fu8 S11 (test-code only): overrides the hardcoded Artifactory base URL in `sdk_test_data_provider.cpp` so firewalled hosts (TCP/443 to internal Artifactory hangs) can point the image/media tests at a local mirror. |
| `MEGA_WSTEST_DEFAULT_WINDOWS` | unset | "1" | fu8 S11 (test-code only): nightly arm — the two watchdog integration tests keep the PRODUCT default windows (45s/60s) instead of self-configuring 5s via debug hook, restoring true default-window coverage (meaningful since the S11 watchdog rework; before it, no default-window run could pass). Budgets self-scale (ackstall observe 120s; tail deadline 360s). |

Pre-S8 knobs (`MEGA_WS_DATASET_CONN_GATE`, `MEGA_WS_GATE_WINDOW_MS`, `MEGA_WS_GATE_GAIN_PCT`,
`MEGA_WS_GATE_STEP`) keep their exact semantics; `GATE=0` remains the byte-identical jump-to-K
A/B arm on every platform.

**"~= gate-v1" bisection recipe** (approximate: per-tick sticky sampling remains):
`MEGA_WS_GATE_BP_QUORUM_PCT=100 MEGA_WS_GATE_ENGAGE_WINDOWS=1 MEGA_WS_GATE_PROBE_WINDOWS=1
MEGA_WS_GATE_RETREAT_MAX_WINDOWS=5 MEGA_WS_GATE_TRIM_WINDOWS=0 MEGA_WS_GATE_CEILING_MULT=0`

### S8 amendments (post-smoke, evidence-driven)
- **A1 evidence floor** — `MEGA_WS_GATE_MIN_EVENTS` (default 8, clamp [1,64]): minimum server-confirm
  events per probe judgment; horizons auto-extend (hard cap 10 windows → FAIL). Kills the lumpy-ack
  zero-baseline vacuous confirms that drove cleanNet8m to the ceiling in the first smoke.
- **A2 judged geometric chain** — each CONFIRMED probe doubles the next chained step (1,2,4,8; cap 8,
  clamped to headroom; reset on fail/trim/disengage). Keeps the lossy-link climb a few judged probes
  instead of many evidence-paced +1s. Caps never grow it (first probe fails there).
- Bisection recipe addendum: add `MEGA_WS_GATE_MIN_EVENTS=1` for the "~= gate-v1" arm.
