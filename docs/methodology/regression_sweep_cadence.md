# Regression sweep cadence

The regression sweep cadence is encoded in hard rules HR14, HR33, HR34,
HR40, HR43, HR46, HR47, HR51 (fu7-18). Each rule has a specific trigger
and a specific incident that motivated it. Together they define the
minimum gate on any wsupload-touching change.

## At a glance

| Rule | When | What | Origin |
|---|---|---|---|
| HR14 | Every commit | SLU n=3 + B9 n=10 + MN n=1 minimum on the affected surface; **fu7-18: + Tier S Jenkins-MR empty-filter smoke before push (HR51)**. | fu7-7 standardisation of the per-commit bench gate. |
| HR34 | Between Goals within a session | Same as HR14 but on the cumulative Goal output, before moving on. | fu7-10 — caught a cross-Goal interaction missed by per-commit gates. |
| HR33 | Final session sweep (tiered) | Tier S empty-filter Jenkins-MR mirror (3 sub-runs) + Tier 1 every `SdkWsUploadTest.*` n=15 + Tier 2 MN n=5 ONE batch + every other `SyncTest.*` n=3 + Tier 3 HR38 paired n=5 + Tier 4 bench paired (SLU n=15) + Tier 5 TSAN refresh. | fu7-11 baseline; fu7-18 tiered after the StopStart + TwoWay_Symmetries Jenkins-MR escapes. |
| HR40 | Final sweep MN | MN runs as ONE batch, never stitched from 3+2. | fu7-15 — batch-stitching hides envelope drift. |
| HR43 | Timing-assertion cells | `--gtest_repeat=15` minimum (raised from HR43's original 10); **fu7-18: applies to ALL 30 `SdkWsUploadTest.*` cells**, not the enumerated 7. | fu7-13 → fu7-15 (B9 Distress race); fu7-18 (StopStart escape). |
| HR46 | B9 specifically | Add `taskset -c 0 nice -n 19` for macOS-style single-CPU scheduling stress. | fu7-15 (macOS-only B9 Distress race). |
| HR47 | Agent wait budgets | ≥90 min minimum on long-running tests (MN, large benches). | fu7-16 — 60-min budget killed MN iter 4. |
| HR51 | Jenkins-MR mirror gate | Empty-filter `test_integration` run on Linux (3 sub-runs to mitigate long-process SIGSEGV) before declaring `TECHQA_READY`. | fu7-18 — `StopStartSameEngineDuringTransfer` + `TwoWay_Highlevel_Symmetries` caught by Jenkins post-push instead of agent sweep. |

## HR14 — per-commit bench gate (amended fu7-18: + Tier S smoke before push)

Triggered by: every commit that touches the WS upload surface
(`src/transfer/ws/`, `include/mega/transfer/ws/`, `tests/integration/wsupload/`,
or any sibling that the upload path links). Minimum:

- `SdkBenchmarkTest.SingleLargeUpload` n=3 (SLU).
- `SdkWsUploadTest.B9ClosedThrottleReconnectPacing` n=10 (B9; raised to
  n=15 by HR43 if the change touches the timing path).
- `SyncTest.BasicSync_MassNotifyFromLocalFolderTree` n=1 (MN smoke).

**fu7-18 amendment**: before any push (not every commit; per-commit is
impractical at that scale), additionally run the Tier S empty-filter
Jenkins-MR smoke (~749 cells × n=1, split into 3 sub-runs — see HR33
and HR51 below). The narrow HR14 cell selection catches functional
regressions but cannot enumerate structural ones (TU-boundary effects,
inlining shifts) that the Tier S floor surfaces.

Rationale: even cosmetic refactors can shift lock ordering or inlining
and regress throughput. The bench is short enough to run on every
commit. See [feedback-benchmarks-pre-commit] memory entry.

## HR34 — inter-Goal sweep

Triggered by: completing a Goal (Goal 1 → Goal 2 boundary etc.). Run
the HR14 gate cells PLUS any cell directly affected by the Goal output.

Rationale: per-commit gates can miss cross-Goal interactions (e.g.
Goal 1 reorganises includes, Goal 2 adds new code that compiles fine
locally but degrades throughput when combined with Goal 1's layout).

## HR33 — final session sweep (tiered, fu7-18)

The closing sweep that justifies a `VALIDATED_TECHQA_READY` verdict.
Coverage is structured in tiers — every tier is REQUIRED. The fu7-18
tiered restructure is motivated by `StopStartSameEngineDuringTransfer`
and `TwoWay_Highlevel_Symmetries` Jenkins-MR escapes: fu7-17-2 swept
8 cells of 749 (1.07%) and missed both failing cells. The new floor
mirrors Jenkins MR breadth.

### Tier S — Jenkins-MR mirror (HR51, NEW mandatory floor)

Run the full `test_integration` binary with **empty `--gtest_filter`**
(matching what Jenkins MR runs by default) on Linux. Split into 3
sub-runs to mitigate the BENCHMARKS.md "long-process SIGSEGV" quirk
(test_integration processes >60 min, ≥13 large SyncTest cells in one
invocation have been seen to SIGSEGV during later setUp):

1. `SdkWsUploadTest.*:SdkBenchmarkTest.*:SyncTest.*` (~109 cells, ~3-4 h).
2. `SdkTest.*` + sync cluster (`SdkTestSync*:SdkTestBackup*:SdkTestSyncUploadsOperations.*:SdkTestSyncUploadThrottling.*:SdkTestSyncRootOperations.*:SdkTestSyncLocalRootChange.*:SdkTestBackupSyncLocalRootChange.*:SdkTestSyncPrevalidation.*:SyncFingerprintCollisionTest.*:SdkTestSyncLocalOperations.*:SdkTestSyncNodeAttributes.*:SdkTestSyncVersionedNodeDeletion.*`) (~230 cells, ~3-4 h).
3. Everything else (`FileServiceTests.*:SdkHttpServerTest.*:PartialDownloadTests.*:SdkTestPasswordManager*:SdkTestUserAttribute.*:SdkTestNodeTags*:FilterFixture.*:LocalToCloudFilterFixture.*:CloudToLocalFilterFixture.*:FilterFailureFixture.*:…`) (~410 cells, ~2-3 h).

Each sub-run uses `setsid` (HR50) and has its own `pid_<PID>` dir.

Tier S is the **Jenkins-MR-mirror gate** (HR51) and is non-deferrable.
The HR21 "audit-only Δ=0 claim" rationale is NOT a valid defer for Tier S.

### Tier 1 — WS upload core (n=15 on every SdkWsUploadTest sibling)

- All 30 `SdkWsUploadTest.*` cells run at n=15 (raised from HR33-original
  "n=3 non-critical; n≥15 timing").
- HR43 applies to the entire suite, not just the original enumerated
  7 timing cells. Cell-name semantics are not a reliable filter — every
  WS upload cell crosses the same lock and worker-thread surfaces.
- B9 additionally tasksetted per HR46.

### Tier 2 — Sync engine (MN ONE batch + every SyncTest sibling n=3)

- `SyncTest.BasicSync_MassNotifyFromLocalFolderTree` n=5 ONE batch (HR40).
- All 77 other `SyncTest.*` cells at n=3.
- BENCHMARKS.md "Test-infrastructure quirks": known API_ETOOMANY
  `BasicSync_Move*` cells excluded from bulk and run individually at
  n=3 each (separate launches between bulk passes).

### Tier 3 — HR38 transfer cells (paired develop comparison)

- 6 cells × n=5 on BOTH fu7-N binary AND develop binary (HR4 + HR31 +
  HR38 + HR45 RSS+CPU).
- `SdkResumableTrasfers` additionally at n=10 (HR15 effect-detection
  power threshold).

### Tier 4 — Bench throughput (paired SLU n=15 + 3 throughput cells)

- SLU n=15 on both binaries (HR14 + HR15 + HR43).
- ManySmall / 1kSmall / LargePlusManySmall n=3 each on both binaries
  (HR14 minimum). Requires `dev-unix-wsupload-benchOn` preset for
  bench framework registration.

### Tier 5 — TSAN refresh (10 v2-surface cells)

- 10 v2-surface cells × n=3 on `build-sdk-dev-unix-tsan` (HR23).

### Tier 6 — LOW-risk smoke (fall-back when Tier S blocked)

If Tier S sub-runs cannot complete due to infrastructure (account-lock
contention, long-process SIGSEGV mid-run), Tier 6 provides compensating
coverage:

- `SdkTestUserAttribute.*`, `SdkTestPasswordManager*`, `SdkTestNodeTags*`,
  `SdkHttpServerTest.*`, `FileServiceTests.*`,
  `SdkTestAvatar.*`/`SdkTestPath.*`/`SdkTestFilter.*`/`SdkTestOrder.*` —
  all at n=3.
- Tier 6 is NOT a substitute for Tier S in the general case; it is a
  documented fall-back when infrastructure prevents Tier S.

### Stop conditions

- Any cell <80% PASS → halt + escalate per HR21.
- MN HUNG single iter >90 min → halt + HR40 + HR47.
- Develop comparison ≥5% regression on any axis → investigate per HR15.
- NEW v2-surface TSAN race vs develop → HR23 fix.
- HR24 >700 → halt + audit.

### Walltime budget

Tier S: ~6-10 h (sequential, 3 sub-runs). Tier 1: ~6-8 h. Tier 2: ~5-6 h.
Tier 3: ~5 h. Tier 4: ~4-5 h. Tier 5: ~1 h. **Total: 27-35 h sequential**
per `feedback_tests_sequential`. The user has explicitly waived budget
constraints for the final sweep ("forget about budget limitations for this").

## HR40 — MN ONE batch

`SyncTest.BasicSync_MassNotifyFromLocalFolderTree` is the canonical
livelock regression test. It must run as ONE `--gtest_repeat=5`
invocation, NOT as `--gtest_repeat=3` + `--gtest_repeat=2`. Batch
stitching averages out envelope drift that one-batch runs reveal.

Wall envelope target: 4:46-17:53 min (fu7-16's observed range).
HUNG threshold: >90 min single iter → escalate.

## HR43 + HR46 — timing-cell stress (amended fu7-18: all SdkWsUploadTest siblings)

Cells whose assertions depend on WS-pool worker thread scheduling
need:

- `--gtest_repeat=15` minimum (HR43, raised by HR46 from original 10).
- B9 additionally needs `taskset -c 0 nice -n 19` to simulate the
  macOS-style single-CPU scheduling pressure that fu7-14 push uncovered.

**fu7-18 amendment**: HR43 applies to **all 30 `SdkWsUploadTest.*` cells**,
not the enumerated subset (T1, IP, Overquota, Resume, RepeatedPauseResume,
MultiplePauseResume, B9). Cell-name semantics are not a reliable predictor
of timing sensitivity — every WS upload cell crosses the same lock and
worker-thread surfaces. The fu7-17-2 sweep's "non-critical n=3" partition
missed `StopStartSameEngineDuringTransfer` specifically (1/15 = 6.7%
failure rate at n=15 pre-fix); the amendment is structural, not
enumerative.

n=1 hides flakes. n=10 was insufficient for the fu7-14 → fu7-15 macOS
B9 Distress race. n=15 + taskset is the new floor.

## HR51 — Jenkins-MR mirror gate (NEW, fu7-18)

Triggered by: every fu7-N close before declaring `TECHQA_READY`.

Run the full `test_integration` binary on Linux with **empty
`--gtest_filter`** to mirror what Jenkins MR runs by default. Split
into 3 sub-runs per the Tier S structure in HR33 to mitigate
long-process SIGSEGV.

HR49 catches Jenkins failures **after** push, forcing user-side
iteration. HR51 catches them **before** push by ensuring the agent's
coverage floor structurally mirrors Jenkins' default-filter breadth.

Origin: fu7-18 — `SdkWsUploadTest.StopStartSameEngineDuringTransfer`
(macOS, n=15 reproduced 1/15 on Linux) and
`SyncTest.TwoWay_Highlevel_Symmetries` (Linux + macOS) were caught by
Jenkins MR post-push instead of by the fu7-17-2 agent sweep. The
fu7-17-2 sweep covered 8 of 749 cells (1.07%); HR51 makes the floor
structural rather than enumerative.

## HR47 — agent wait budget ≥90 min

Long-running tests (MN, full SLU n=15, the full WS-other sweep) must
have an agent wait budget of at least 90 min. fu7-15's 60-min budget
killed MN iter 4 mid-run forcing 3+2 batch stitching; fu7-16 raised the
floor and ran MN n=5 as ONE batch with no stitching.

## How to run the sweep

```bash
cd ~/repo/build-sdk-dev-unix-wsupload/tests/integration
source environment2.txt

# HR14 per-commit
./test_integration --CI --COUT --USERAGENT:JenkinsCanSpam-SDK \
  --gtest_filter="SdkBenchmarkTest.SingleLargeUpload" --gtest_repeat=3

./test_integration --CI --COUT --USERAGENT:JenkinsCanSpam-SDK \
  --gtest_filter="SdkWsUploadTest.B9ClosedThrottleReconnectPacing" --gtest_repeat=15

# B9 + taskset (HR46)
taskset -c 0 nice -n 19 ./test_integration --CI --COUT --USERAGENT:JenkinsCanSpam-SDK \
  --gtest_filter="SdkWsUploadTest.B9ClosedThrottleReconnectPacing" --gtest_repeat=15

# MN ONE batch (HR40)
./test_integration --CI --COUT --USERAGENT:JenkinsCanSpam-SDK \
  --gtest_filter="SyncTest.BasicSync_MassNotifyFromLocalFolderTree" --gtest_repeat=5

# Tier S — Jenkins-MR mirror (HR51, fu7-18; 3 sub-runs to mitigate long-process SIGSEGV)
# Run sequentially per feedback_tests_sequential; each via fu_run() / setsid (HR50).
# S1a — WS upload + bench + SyncTest (~109 cells, ~3-4 h)
setsid ./test_integration --CI --COUT --USERAGENT:JenkinsCanSpam-SDK \
  --gtest_filter="SdkWsUploadTest.*:SdkBenchmarkTest.*:SyncTest.*"

# S1b — SdkTest + sync cluster (~230 cells, ~3-4 h)
setsid ./test_integration --CI --COUT --USERAGENT:JenkinsCanSpam-SDK \
  --gtest_filter="SdkTest.*:SdkTestSync*:SdkTestBackup*:SdkTestSyncUploadsOperations.*:SdkTestSyncUploadThrottling.*:SdkTestSyncRootOperations.*:SdkTestSyncLocalRootChange.*:SdkTestBackupSyncLocalRootChange.*:SdkTestSyncPrevalidation.*:SyncFingerprintCollisionTest.*:SdkTestSyncLocalOperations.*:SdkTestSyncNodeAttributes.*:SdkTestSyncVersionedNodeDeletion.*"

# S1c — file service + http + password + filters + other (~410 cells, ~2-3 h)
setsid ./test_integration --CI --COUT --USERAGENT:JenkinsCanSpam-SDK \
  --gtest_filter="FileServiceTests.*:SdkHttpServerTest.*:PartialDownloadTests.*:SdkTestPasswordManager*:SdkTestUserAttribute.*:SdkTestNodeTags*:FilterFixture.*:LocalToCloudFilterFixture.*:CloudToLocalFilterFixture.*:FilterFailureFixture.*:ContradictoryMoveFixture.*:BackupBehavior.*:DisableBackupSync.*:OneQuestionSurveyTest.*:CreateAccount/SdkTestCreateAccount.*:ScChannel/SdkTestScChannel.*:UploadFilesInNestedShare/SdkTestShareNested.*:StorageAccessProtocol/SdkTestStorageServerAccessProtocol.*:SdkTestAvatar.*:SdkTestPath.*:SdkTestFilter.*:SdkTestOrder.*:SdkTestUnicodeSort.*:SdkTestListAllNodesByPage.*:SdkTestListAllNodesByPageScope.*:SdkTestNodeGpsCoordinates.*:SdkTestLocklessCSChannel.*"

# Tier 1 — all SdkWsUploadTest n=15 (fu7-18 HR43 expansion)
setsid ./test_integration --CI --COUT --USERAGENT:JenkinsCanSpam-SDK \
  --gtest_filter="SdkWsUploadTest.*" --gtest_repeat=15
```

Logs land at `${HOME}/mega_tests/pid_<PID>/test_integration.log`.

## Outcome ledger

Record per-cell outcomes in the Goal directory:

| cell | n | PASS | wall median | wall p95 | throughput median |
|---|---|---|---|---|---|

This table belongs in `Goal5_final_regression_sweep_v<N>/fu7_N_final.md`
and is the basis of the Verdict's "Goal 5 sweep result" line.

## HR50 — Shell-detachment mitigation for long-running children

Triggered by: agent-driven launches of `test_integration` (or any
binary expected to run >2 minutes) under `bash &` /
`run_in_background`.

Mandatory: use `setsid` (or `nohup` on macOS-style systems lacking
`util-linux`'s `setsid`) so the child becomes its own session leader
(`SID == PID`, `PPID == 1` after wrapper exit). The launching agent
must verify with `ps -o pid,ppid,sid,pgid -p <pid>` before declaring
the run "started."

Failure mode if skipped: the child shares the launching shell's
session. When the shell is reaped — by agent harness timeout, session
reset, or any external cleanup — the kernel sends SIGHUP to the
process group. Default action is `Term` (terminate without core
dump). The process vanishes with NO crash markers (FATAL / SEGV /
abort), NO OOM in `dmesg`, NO journalctl session-scope teardown. The
launching agent's `run_in_background` task reports "completed" but
the test never finished.

Observed at: SDK-5360 fu7-17 Failure 2 (`pid_996793`, 2026-05-18
22:26:47 UTC), MN test killed mid-enqueue at file 10,941/16,000 after
~1 minute, ~4 hours of wall budget consumed before detection.

### Canonical wrapper

```bash
# Required: env vars set in the SAME bash invocation as the launch
fu_run() {
  local FILTER="$1"
  local REPEAT="${2:-1}"
  local LABEL="${3:-unlabeled}"
  local BUILD="${4:-build-sdk-dev-unix-wsupload}"
  local STDOUT_LOG=/tmp/fu_${LABEL}.log

  setsid bash -c "
    cd ~/repo/${BUILD}/tests/integration
    source ./environment2.txt
    if [[ -z \"\$MEGA_PWD\" ]]; then
      echo 'FATAL: MEGA_PWD not set after sourcing environment2.txt' >&2
      exit 99
    fi
    exec ./test_integration --CI --COUT --USERAGENT:JenkinsCanSpam-SDK \\
      --gtest_filter=\"$FILTER\" --gtest_repeat=$REPEAT
  " </dev/null >"$STDOUT_LOG" 2>&1 &
  disown

  sleep 4
  local PID
  PID=$(ps -eo pid,args | awk -v f="$FILTER" '$2 ~ /test_integration$/ && $0 ~ ("--gtest_filter="f) {print $1; exit}')
  [[ -n "$PID" ]] || { echo "FATAL: child not launched"; return 1; }

  local SID PPID_VAL
  SID=$(ps -o sid= -p $PID | tr -d ' ')
  PPID_VAL=$(ps -o ppid= -p $PID | tr -d ' ')

  if [[ "$SID" != "$PID" ]]; then
    echo "FATAL: SID=$SID != PID=$PID; setsid failed"
    return 2
  fi

  echo "OK: $LABEL launched PID=$PID PPID=$PPID_VAL SID=$SID log=$STDOUT_LOG"
  echo "$PID"
}
```

### Watchdog

```bash
PID=<from fu_run>
PID_LOG="${HOME}/mega_tests/pid_${PID}/test_integration.log"
STDOUT_LOG=/tmp/fu_<label>.log

while true; do
  if [[ ! -d /proc/$PID ]]; then
    grep -qE "PASS|GTEST: PASSED|\[       OK \]" "$STDOUT_LOG" && { echo DONE_PASS; break; }
    grep -qE "FAIL|GTEST: FAILED|\[  FAILED  \]" "$STDOUT_LOG" && { echo DONE_FAIL; break; }
    echo "VANISHED — re-verify SID/PPID and consider HR50 wrapper bug"
    break
  fi
  sleep 30
done
```

For MN cells specifically, enforce the HR40 + HR47 90-min watchdog
cap.

### Scope and exceptions

Required for: `test_integration` cells expected to run >2 min,
specifically MN, B9 stress, SLU multi-iter, IP, the
`SdkWsUploadTest.*` sweep, and any HR38 cell.

NOT required for: short utility commands (cmake build,
`--gtest_list_tests`, env sanity checks) that finish before the
launching shell can be reaped.

Jenkins-driven launches already detach via Jenkins's own process
management — HR50 applies primarily to **agent-driven** launches, not
Jenkins-driven.
