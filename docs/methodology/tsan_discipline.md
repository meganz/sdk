# TSAN discipline

ThreadSanitizer (TSAN) finds data races dynamically. It is the principal
gate against thread-safety regressions on the WS upload surface.

## Where TSAN is available

| OS | Compiler | TSAN preset | Notes |
|---|---|---|---|
| Linux | gcc / clang | `dev-unix-tsan` | Primary platform. |
| macOS | AppleClang | `dev-macos-tsan` | clang's TSAN works natively. |
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

## Surface filter vs full sweep

The full TSAN sweep is expensive. For per-commit gates, use the
4-cell **WS-upload surface filter** (matches the Jenkinsfile
`TSAN_SURFACE_FILTER`):

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

## NEW-vs-develop race classification

Compare TSAN race signatures between the candidate binary and a
baseline binary (typically `origin/develop`). The top-frame signature
is the minimum identity for grouping: two race events are "the same"
if their top frame and second-frame function names match.

- **NEW races** (present on candidate, absent on baseline) are
  blocking — fix before merge.
- **Pre-existing races** (present on both) are enumerated but not
  fixed in the candidate session unless explicitly scoped.

Run both binaries at the same `n` (typically n=2-3 on the surface
filter) on the same OS, same TSAN_OPTIONS, same day. Use
`scripts/ci/compare_tsan.sh` to bucket signatures by top frame.

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
  is preferred but discouraged for code on the WS-upload surface.

## Coverage today vs expansion ideas

**What the surface filter actually exercises today.** The 4 cells
(`ActivePoolUsesParallelConnections`, `B9ClosedThrottleReconnectPacing`,
`InvalidPinnedSessionFallsBackToFreshSession`, `OverquotaDuringTransfer`)
together cover the WS pool lifecycle, worker thread spin-up/tear-down,
session URL fallback, and overquota signalling — i.e. the WS **upload**
state machine. They do NOT exercise:

- Downloads (any code path through `DirectReadSlot` / `MegaFileGet`).
- Sync engine (`SyncDownload_inClient`, `syncinternals/*`).
- File system observers (`Notifier*`, posix/win32 platform code).
- HTTP-layer commands that aren't on the WS code path
  (`CommandPutFile`, `CommandSetAttr`, sync action-packet processing).

**Expansion ideas** (file as candidate tickets):

1. Add a `TSAN_DOWNLOAD_SURFACE_FILTER` with 2-3 cells exercising
   `SdkTest.SdkResumableTrasfers` + `SdkTest.SdkTestTransferStats` so
   download races surface alongside upload ones in the per-MR gate.
2. Add a `TSAN_SYNC_SURFACE_FILTER` with the `BasicSync_*` smoke
   cells. SyncTest cells are slower under TSAN — consider a longer
   `timeout=` budget on the stage.
3. Drop the field-list guard on the Linux Jenkinsfile (`mChunksInFlight`
   etc.) — that whitelist is conservative; once the broader surface
   is enumerated, the gate can fail on **any** race in
   `src/transfer/`, `src/sync.cpp`, `src/syncinternals/`,
   `src/file.cpp`, narrowing-not-broadening over time.
4. Run TSAN in a nightly cron (parallel to MR triggers) over the
   FULL `SdkTest.*` + `SdkWsUploadTest.*` + `SyncTest.*` suite at n=1
   so we accumulate a churn signal independent of per-MR gating.
   Archive race signatures into the bench-trend store so we can
   spot regressions on develop weeks before they're hit on an MR.
- Don't quote TSAN counts from different binaries with different
  `TSAN_OPTIONS` settings; `history_size` affects sensitivity.
