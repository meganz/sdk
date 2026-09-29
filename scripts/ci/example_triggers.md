# GitLab MR trigger comments — copy-paste recipes

Each recipe below is a single MR comment. Post it on the MR and the
matching Jenkins jobs will pick it up via the configured trigger phrase
regex (server-side; conventional phrase: `trigger compilation`). See
[docs/ci/CI_TESTING_GUIDE.md](../../docs/ci/CI_TESTING_GUIDE.md).

## Full default sweep

The cheapest answer: just push. Every push to an MR runs the default
build + regular tests on Linux + macOS + Windows + iOS + Android +
megachat + clang-format. No comment needed.

If you still want to re-trigger without a new push:

    trigger compilation

Hits: all OSes.

## Single-cell SLU bench

    trigger compilation --bench --gtest_filter=SdkBenchmarkTest.SingleLargeUpload

Hits: Linux + macOS + Windows. Runs just the SLU cell three times and
emits `bench_report_*.json`.

## B9 stress n=15 with taskset isolation

    trigger compilation --gtest_filter=SdkWsUploadTest.B9ClosedThrottleReconnectPacing --gtest_repeat=15

Hits: all OSes. (Minimum recommended repeat count for the B9 timing assertion.) The local
`run_bench.sh` equivalent already pins CPU0 via `taskset`; the Jenkins
worker is single-tenant, so no explicit pin needed.

## MN smoke n=5 ONE batch

    trigger compilation --gtest_filter=SdkWsUploadTest.MassNotify --gtest_repeat=5

Hits: all OSes. Runs the MassNotify cell five times to catch livelock
regressions.

## All WS-upload surface TSAN

    trigger compilation --tsan

Hits: Linux + macOS. Not Windows (MSVC has no TSAN runtime).

## TSAN with custom filter

    trigger compilation --tsan --gtest_filter=SdkWsUploadTest.OverquotaDuringTransfer

Hits: Linux + macOS. Useful when narrowing down which cell triggers a
race.

## Bench with custom filter

    trigger compilation --bench --gtest_filter=SdkBenchmarkTest.Mixed*

Hits: Linux + macOS + Windows. Run a glob of bench cells.

## Cross-OS smoke (bench on Mac + Win + Linux)

    trigger compilation --bench

Hits: all 3 bench-capable OSes. Useful when validating that a wsupload
change has no platform-specific regression.

## Sequential regular tests for flake debugging

    trigger compilation --sequence

Hits: all OSes. Runs the regular suite serially (no `-j`) — slower but
reproducible for tests sensitive to other tests' state.

## Custom timeout for long jobs

Trigger comments don't expose timeout; long jobs are configured in the
Jenkinsfile pipeline timeout block. To raise it, edit the relevant
`jenkinsfile/Jenkinsfile_MR_*` and submit an infra MR.
