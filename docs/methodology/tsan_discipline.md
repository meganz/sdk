# TSAN discipline

ThreadSanitizer (TSAN) finds data races dynamically. On SDK-5360 it is
the principal gate against thread-safety regressions on the WS upload
v2 surface.

## Where TSAN is available

| OS | Compiler | TSAN preset | Notes |
|---|---|---|---|
| Linux | gcc / clang | `dev-unix-tsan` | Primary platform. |
| macOS | AppleClang | `dev-macos-tsan` (post-fu7-17) | clang's TSAN works natively. |
| Windows | MSVC | **NOT SUPPORTED** | MSVC has no TSAN runtime; clang-cl's is incomplete. |

For Windows, rely on the regular sweep + Linux/macOS TSAN coverage.

## Build

```bash
cmake --preset dev-unix-tsan
cmake --build ~/repo/build-sdk-dev-unix-tsan -j16 --target test_integration
```

This sets `-DENABLE_TSAN=ON` which adds `-fsanitize=thread` and the
matching link flags.

## Run

```bash
TSAN_OPTIONS='halt_on_error=0:second_deadlock_stack=1:history_size=7:report_thread_leaks=0' \
  ~/repo/build-sdk-dev-unix-tsan/tests/integration/test_integration \
  --USERAGENT:JenkinsCanSpam-SDK --CI --COUT \
  --gtest_filter='SdkWsUploadTest.ActivePoolUsesParallelConnections'
```

Recommended TSAN_OPTIONS (matches the Jenkinsfile lines ~420):

| Option | Why |
|---|---|
| `halt_on_error=0` | Keep running after the first race so we get all of them in one log. |
| `second_deadlock_stack=1` | Capture the second-deadlock stack trace; helps disambiguate lock-cycle races. |
| `history_size=7` | More memory-access history; slower but more diagnostic. |
| `report_thread_leaks=0` | Don't flag worker threads as leaked — known false positives. |

## v2-surface filter vs full sweep

The full TSAN sweep is expensive. For per-commit gates, use the
4-cell **v2-surface filter** (matches the Jenkinsfile
`TSAN_V2_SURFACE_FILTER` introduced in fu7-12):

```
SdkWsUploadTest.ActivePoolUsesParallelConnections          # T1
SdkWsUploadTest.B9ClosedThrottleReconnectPacing            # B9
SdkWsUploadTest.InvalidPinnedSessionFallsBackToFreshSession # IP
SdkWsUploadTest.OverquotaDuringTransfer                    # Overquota
```

This is the minimum surface that exercises the WS pool + worker thread
+ session URL fallback paths where new races would land.

For final session sweeps, extend with the full `SdkWsUploadTest.*`
suite at lower n.

## NEW-vs-develop classification protocol

A race detected on the fu7-N binary is one of:

| Class | Action |
|---|---|
| Present on both develop and fu7-N | **Pre-existing** — document, don't fix in this session. |
| Present on fu7-N, NOT on develop | **NEW** — HR23 says fix this session. |

Procedure:

1. Build `~/repo/sdk-develop` at current `origin/develop` HEAD.
2. Build `~/repo/sdk-develop-dev-unix-tsan` (cherry-pick the bench
   instrumentation if needed for parity per HR4).
3. Run the v2-surface filter on the develop binary, n=2.
4. Capture top-frame signatures in
   `…/followup7-N/Goal4_tsan_deep_dive/develop_baseline_races_fu7_N.md`.
5. Run the same filter on the fu7-N binary, n=2.
6. Diff signatures. Classify each.

## Race signature

The top-frame signature is the minimum identity for grouping. Two
race events are "the same" if their top frame and second-frame
function names match. Use `scripts/ci/compare_tsan.sh` to bucket.

## Pre-existing race tracking

Pre-existing races are NOT ignored — they are enumerated. Each session
records:

- Total race count (e.g. fu7-16: 68 events).
- Unique top-frame signatures (e.g. fu7-16: 6 signatures).
- Top-frame breakdown by site (e.g. SdkTest_test.cpp accessors,
  MegaFolderInfo getters).

Drift in this catalogue signals churn even when no NEW race appeared.

## When a NEW race surfaces

Per HR23 the session must fix it before close. Procedure:

1. Reproduce with `--gtest_repeat=3` to confirm reproducibility.
2. Inspect the access pattern via `LOG_debug` at the racing sites.
3. Apply the minimum-invasive fix (atomic, mutex, ordering).
4. Re-run TSAN to confirm closure.
5. Re-run the bench gate (HR14) — the fix must not regress throughput.

If the structural fix is too invasive for the session wall budget:
HR21 escalation — document in `…/Goal4/…/escalation.md` and revert
the change that introduced the race.

## Comparison: scripts/ci/compare_tsan.sh

```bash
scripts/ci/compare_tsan.sh tsan_sweep_A.log.gz tsan_sweep_B.log.gz
```

Output buckets race events by top-frame signature, with counts per
input. Lines unique to one input are flagged.

## What NOT to do

- Don't run TSAN on a Release build (defines NDEBUG); TSAN expects
  the test framework's debug-only `globalMegaTestHooks` symbols.
- Don't run TSAN cells in parallel — same reason as regular bench
  (rate-limited accounts, see [feedback_tests_sequential.md]).
- Don't suppress races globally via a `.tsan_suppressions` file
  without explicit team sign-off; per-site `__attribute__((no_sanitize("thread")))`
  is preferred but discouraged for v2-surface code.
- Don't quote TSAN counts from different binaries with different
  `TSAN_OPTIONS` settings; `history_size` affects sensitivity.
