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

## Expansion ideas (fu7-18+)

- Jenkins trend-graph plugin (throughput-over-time per cell).
- Slack-bot for regression > 5% vs last-N median.
- Per-cell baseline file checked into repo (last-known-good throughput).
- Side-by-side artifact diff UI extension.
- Windows TSAN if/when MEGA migrates Windows builds to clang.
