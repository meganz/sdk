# MEGA SDK — CI testing guide

## How to trigger a Jenkins build from a GitLab MR

Post a comment on the MR. The Jenkins GitLab plugin watches for a
**trigger phrase regex** configured on the GitLab side (Project →
Settings → Integrations → Jenkins → "Trigger comment / Note regex").
The conventional phrase is:

    trigger compilation

(If you're unsure, ask a teammate or check the GitLab Integrations page;
the regex itself is NOT version-controlled in this repo.)

Each Jenkinsfile (Linux / macOS / Windows / iOS / Android / megachat /
clang-format) is registered independently with the same trigger phrase.
One comment fires all of them; each pipeline scans the comment body and
respects the flags it understands.

## Flag cheatsheet

| Comment                                                       | What it adds         | OS coverage         |
|---------------------------------------------------------------|----------------------|---------------------|
| `trigger compilation`                                         | full default build   | all (Lin/Mac/Win)   |
| `trigger compilation --bench`                                 | bench stage          | Lin + Mac + Win     |
| `trigger compilation --tsan`                                  | TSAN sweep           | Lin + Mac (NOT Win) |
| `trigger compilation --gtest_filter=Foo.Bar`                  | filter regular tests | all                 |
| `trigger compilation --gtest_filter=Foo --gtest_repeat=15`    | filter + repeat      | all                 |
| `trigger compilation --bench --gtest_filter=…`                | filter bench         | all (post-fu7-17)   |
| `trigger compilation --tsan --gtest_filter=…`                 | filter TSAN          | Lin + Mac           |
| `trigger compilation --windows-32bits`                        | 32-bit build         | Win                 |
| `trigger compilation --sequence`                              | serial tests         | all                 |
| `trigger compilation --rebuild-docker`                        | clang-format docker  | clang-format job    |

## Why isn't Windows TSAN supported?

MSVC has no TSAN runtime; clang-cl's TSAN support is incomplete. If
MEGA migrates Windows builds to a clang toolchain later, `--tsan` can be
extended to Windows then.

## Preset glossary (LOCAL dev builds)

Each preset produces its own build dir at
`${HOME}/repo/build-sdk-<presetName>` (or the Windows equivalent). They
coexist; you can have all of them present at once.

| Preset name                       | What's different from base dev                                            | OS      |
|-----------------------------------|---------------------------------------------------------------------------|---------|
| `dev-unix`                        | Linux base Debug build (default tests, default options)                   | Linux   |
| `dev-unix-wsupload-benchOn`       | + `MEGA_BENCH_FRAMEWORK_ENABLED=ON` (emits `bench_report_*.json`)         | Linux   |
| `dev-unix-tsan`                   | + `ENABLE_TSAN=ON` (`-fsanitize=thread`)                                  | Linux   |
| `dev-unix-strict`                 | + `-Wmismatched-tags` hard error + `-Wsign-conversion` warn               | Linux   |
| `dev-unix-hooks-off`              | + test-hooks disabled (validates `WSUPLOAD_REQUIRE_TEST_HOOKS` gate)      | Linux   |
| `dev-macos`                       | macOS base Debug build                                                    | macOS   |
| `dev-macos-wsupload-benchOn`      | + bench framework on                                                      | macOS   |
| `dev-macos-tsan`                  | + TSAN (AppleClang supports it natively)                                  | macOS   |
| `dev-macos-strict`                | + clang strict warnings                                                   | macOS   |
| `dev-macos-hooks-off`             | + hooks off                                                               | macOS   |
| `dev-windows`                     | Windows base Debug build (MSVC)                                           | Windows |
| `dev-windows-wsupload-benchOn`    | + bench framework on                                                      | Windows |
| `dev-windows-strict`              | + MSVC strict (`/W4 /WX-`)                                                | Windows |
| `dev-windows-hooks-off`           | + hooks off                                                               | Windows |
| _(no `dev-windows-tsan`)_         | MSVC has no TSAN runtime                                                  | —       |

## Running locally — common recipes

| Task                              | Command                                                                                                                                                                                                                                                                                                                       |
|-----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Bench (one or more cells)         | `bash tests/integration/run_bench.sh 'SdkBenchmarkTest.SingleLargeUpload' 3`                                                                                                                                                                                                                                                  |
| TSAN build                        | `cmake --preset dev-unix-tsan && cmake --build ~/repo/build-sdk-dev-unix-tsan -j16 --target test_integration`                                                                                                                                                                                                                  |
| TSAN run                          | `TSAN_OPTIONS='halt_on_error=0:second_deadlock_stack=1:history_size=7:report_thread_leaks=0' ~/repo/build-sdk-dev-unix-tsan/tests/integration/test_integration --USERAGENT:JenkinsCanSpam-SDK --CI --COUT --gtest_filter='SdkWsUploadTest.OverquotaDuringTransfer'`                                                              |
| Strict pre-push gate (HR42)       | `cmake --preset dev-unix-strict && cmake --build ~/repo/build-sdk-dev-unix-strict -j16 --target SDKlib`                                                                                                                                                                                                                       |

(Replace `dev-unix-*` with `dev-macos-*` or `dev-windows-*` on the
respective hosts.)

## Where do artifacts land?

On the Jenkins build page → "Build Artifacts":

| Pattern                              | Source                          |
|--------------------------------------|---------------------------------|
| `test_integration.log`               | regular run                     |
| `bench_report_*.json`                | bench stage                     |
| `bench_logs_<BUILDID>.tar.gz`        | bench per-cell logs             |
| `bench_sweep_<BUILDID>.log.gz`       | bench full log                  |
| `tsan_sweep_<BUILDID>.log.gz`        | TSAN full log                   |
| `tsan_logs_<BUILDID>.tar.gz`         | TSAN per-cell (post-fu7-17)     |
| `core.tar.gz`                        | crash dumps                     |

**Local equivalent**: `bench_report_*.json` files land in
`${HOME}/mega_tests/pid_<PID>/bench_report_<PID>.json`. Find recent
ones with:

    bash scripts/ci/find_bench_reports.sh

## Comparing two runs

1. Download `bench_report_A.json` and `bench_report_B.json` (or TSAN
   logs) from two builds.
2. Run:

        scripts/ci/compare_bench.py bench_report_A.json bench_report_B.json
        scripts/ci/compare_tsan.sh tsan_sweep_A.log.gz tsan_sweep_B.log.gz

## I get `bench_report_*.json` missing — why?

The default `dev-unix` build has `MEGA_BENCH_FRAMEWORK_ENABLED=OFF` so
bench JSON is never emitted. Use `dev-unix-wsupload-benchOn` (or the
macOS / Windows equivalents). Same for Jenkins — the `--bench` stage
uses a separate build dir with the flag ON.

## What does TSAN actually cover today?

The `--tsan` stage runs a narrow **WS-upload surface filter** (the 4
cells that exercise the WS pool / worker thread / session URL
fallback / overquota signalling state machine):

| Cell | Exercises |
|---|---|
| `SdkWsUploadTest.ActivePoolUsesParallelConnections` | Pool fan-out across 8 worker threads |
| `SdkWsUploadTest.B9ClosedThrottleReconnectPacing` | Throttle reconnect pacing |
| `SdkWsUploadTest.InvalidPinnedSessionFallsBackToFreshSession` | Session URL fallback |
| `SdkWsUploadTest.OverquotaDuringTransfer` | Mid-transfer overquota signalling |

The stage **fails** if any race signature touches one of the watched
WS-upload-surface fields (`mChunksInFlight`, `mNumChunksInFlight`,
`mUploadingFile`, `WsConn::mLastActive`, `WsConn::mPausedByServerUntilDs`,
`WsConn::mReadyState`, `WsUploadFile`, `mWorkGeneration`).

**What's NOT covered yet** (expansion candidates):

- **Downloads** — `DirectReadSlot`, `MegaFileGet`, `SyncDownload_inClient`.
- **Sync engine** — `src/sync.cpp`, `src/syncinternals/*`,
  `Notifier*`, posix/win32 filesystem observers.
- **HTTP-layer non-WS commands** — `CommandPutFile`, `CommandSetAttr`,
  sync action-packet processing.

### Ideas to expand TSAN coverage

1. **`TSAN_DOWNLOAD_SURFACE_FILTER`** — add 2-3 cells like
   `SdkTest.SdkResumableTrasfers`, `SdkTest.SdkTestTransferStats` so
   download races land in the per-MR gate alongside upload ones.
2. **`TSAN_SYNC_SURFACE_FILTER`** — `BasicSync_*` smoke cells. Sync
   cells run slower under TSAN — bump the stage `timeout=` budget.
3. **Drop the field-list guard** — today the Jenkinsfile fails only
   if a race hits a curated whitelist. Once broader cells are
   enumerated, gate on **any** race in `src/transfer/`, `src/sync.cpp`,
   `src/syncinternals/`, `src/file.cpp`.
4. **Nightly full TSAN cron** — parallel to per-MR triggers, run the
   FULL `SdkTest.*` + `SdkWsUploadTest.*` + `SyncTest.*` suite at
   n=1 weekly. Archive race signatures into the trend store so
   pre-existing-race drift on develop surfaces between MRs.

## What does the bench stage cover today?

| Cell | Workload |
|---|---|
| `SdkBenchmarkTest.SingleLargeUpload` | One ~1-10 GiB upload |
| `SdkBenchmarkTest.ManySmallUploads` | Many small (16 KiB) uploads |
| `SdkBenchmarkTest.1kSmallUploads` | 1 000 × small uploads |
| `SdkBenchmarkTest.LargePlusManySmall` | Mixed workload |

All four are **upload-only**. The bench framework
(`tests/integration/bench_framework/`) is generic and the cmake file
already anticipates `SdkBenchmarkDownload*` cells — none written yet.

### Ideas to expand bench coverage

1. **Add `SdkBenchmarkDownload*` cells** — single-large-download,
   many-small-downloads, mixed. Reuse the same `BenchSession` /
   `BenchReportWriter` plumbing; new `BenchmarkRunners.h` entries
   for download workloads. The JSON schema is workload-agnostic.
2. **Add a `SdkBenchmarkSync*` set** — measure sync-engine throughput
   on `BasicSync_*` workloads (file-add storm, rename storm, MN-like
   16k-file trees). Probably a separate `bench_sync.cmake` target.
3. **Cross-protocol head-to-head** — once download bench exists,
   add a single combined "round-trip" cell: upload large, then
   download same large, asserting both legs land within budget.
4. **Per-cell baseline file in repo** — check `bench_baseline.json`
   into `tests/integration/` with last-known-good throughput per
   cell. `compare_bench.py` compares the bench artifact against
   the baseline; PR fails if Δ > 5 % without a documented reason.

## Expansion ideas (general fu7-18+)

- Jenkins trend-graph plugin (throughput-over-time per cell).
- Slack-bot for regression > 5 % vs last-N median.
- Side-by-side artifact diff UI extension.
- Windows TSAN if/when MEGA migrates Windows builds to clang.
