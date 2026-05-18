# Regression sweep cadence

The regression sweep cadence is encoded in hard rules HR14, HR33, HR34,
HR40, HR43, HR46, HR47. Each rule has a specific trigger and a specific
incident that motivated it. Together they define the minimum gate on
any wsupload-touching change.

## At a glance

| Rule | When | What | Origin |
|---|---|---|---|
| HR14 | Every commit | SLU n=3 + B9 n=10 + MN n=1 minimum on the affected surface. | fu7-7 standardisation of the per-commit bench gate. |
| HR34 | Between Goals within a session | Same as HR14 but on the cumulative Goal output, before moving on. | fu7-10 — caught a cross-Goal interaction missed by per-commit gates. |
| HR33 | Final session sweep | n≥15 timing cells, n=5 MN ONE batch, n=3-5 legacy `SdkTest.*`. | fu7-11 — the closing sweep is the strongest signal. |
| HR40 | Final sweep MN | MN runs as ONE batch, never stitched from 3+2. | fu7-15 — batch-stitching hides envelope drift. |
| HR43 | Timing-assertion cells | `--gtest_repeat=15` minimum (raised from HR43's original 10). | fu7-13 → fu7-15 (B9 Distress race). |
| HR46 | B9 specifically | Add `taskset -c 0 nice -n 19` for macOS-style single-CPU scheduling stress. | fu7-15 (macOS-only B9 Distress race). |
| HR47 | Agent wait budgets | ≥90 min minimum on long-running tests (MN, large benches). | fu7-16 — 60-min budget killed MN iter 4. |

## HR14 — per-commit bench gate

Triggered by: every commit that touches the WS upload surface
(`src/transfer/ws/`, `include/mega/transfer/ws/`, `tests/integration/wsupload/`,
or any sibling that the upload path links). Minimum:

- `SdkBenchmarkTest.SingleLargeUpload` n=3 (SLU).
- `SdkWsUploadTest.B9ClosedThrottleReconnectPacing` n=10 (B9; raised to
  n=15 by HR43 if the change touches the timing path).
- `SyncTest.BasicSync_MassNotifyFromLocalFolderTree` n=1 (MN smoke).

Rationale: even cosmetic refactors can shift lock ordering or inlining
and regress throughput. The bench is short enough to run on every
commit. See [feedback-benchmarks-pre-commit] memory entry.

## HR34 — inter-Goal sweep

Triggered by: completing a Goal (Goal 1 → Goal 2 boundary etc.). Run
the HR14 gate cells PLUS any cell directly affected by the Goal output.

Rationale: per-commit gates can miss cross-Goal interactions (e.g.
Goal 1 reorganises includes, Goal 2 adds new code that compiles fine
locally but degrades throughput when combined with Goal 1's layout).

## HR33 — final session sweep

The closing sweep that justifies a `VALIDATED_TECHQA_READY` verdict:

- All `SdkWsUploadTest.*` (~30 cells) n=3 non-critical; n≥15 timing.
- `SyncTest.BasicSync_MassNotifyFromLocalFolderTree` n=5 ONE batch
  (HR40).
- `SdkTest.*ransfer*` (HR38) n=5 on BOTH fu7-N binary AND develop
  binary.
- Other critical `BasicSync_*` cells n=3-5.
- `SdkTestSync*`, `SdkTestBackup*` n=3-5.

Stop if any cell <80% PASS or >5% throughput regression vs the previous
fu7-N or develop.

## HR40 — MN ONE batch

`SyncTest.BasicSync_MassNotifyFromLocalFolderTree` is the canonical
livelock regression test. It must run as ONE `--gtest_repeat=5`
invocation, NOT as `--gtest_repeat=3` + `--gtest_repeat=2`. Batch
stitching averages out envelope drift that one-batch runs reveal.

Wall envelope target: 4:46-17:53 min (fu7-16's observed range).
HUNG threshold: >90 min single iter → escalate.

## HR43 + HR46 — timing-cell stress

Cells whose assertions depend on WS-pool worker thread scheduling
(B9 in particular, also T1, IP, Resume, RepeatedPauseResume,
MultiplePauseResume) need:

- `--gtest_repeat=15` minimum (HR43, raised by HR46 from original 10).
- B9 additionally needs `taskset -c 0 nice -n 19` to simulate the
  macOS-style single-CPU scheduling pressure that fu7-14 push uncovered.

n=1 hides flakes. n=10 was insufficient for the fu7-14 → fu7-15 macOS
B9 Distress race. n=15 + taskset is the new floor.

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
