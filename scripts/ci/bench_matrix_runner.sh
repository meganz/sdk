#!/usr/bin/env bash
#
# bench_matrix_runner.sh — the single SERIAL Bench Runner (HR56) for SDK-5360 fu8.
# Drains a queue of (label|binary_dir|profile|gtest_filter|N|timeout_s) work units
# STRICTLY ONE AT A TIME, each shaped by scripts/ci/netem_profile.sh, collecting the
# bench JSONL (throughput + rss_max_kb + rss_delta_kb + user/sys CPU) and the rich
# trace log dir, into a stable collect dir. Resumable (skips units whose .DONE exists).
#
# Usage (run AS THE NORMAL USER; netem_profile.sh sudo's per-op internally):
#   scripts/ci/bench_matrix_runner.sh <queue_file> <collect_dir>
#
# Queue line format (| separated; '#' lines and blanks ignored):
#   LABEL|BINARY_DIR|PROFILE|GTEST_FILTER|N|TIMEOUT_S[|EXTRA_ENV]
#     LABEL        e.g. BASE__QaExactSingleFile__loss20
#     BINARY_DIR   build dir whose tests/integration/test_integration + environment2.txt are used
#     PROFILE      a name in scripts/ci/net_profiles.env (clean|poorRTT|loss5|loss20|loss50|qaexact|loss100)
#     GTEST_FILTER e.g. SdkBenchmarkTest.QaExactSingleFile
#     N            --gtest_repeat
#     TIMEOUT_S    per-unit wall-clock watchdog (a stall is recorded, not a hang)
#     EXTRA_ENV    optional, space-sep KEY=VAL passed before test_integration (e.g. LD_PRELOAD=... ASAN_OPTIONS=...)
#
# Output (under <collect_dir>/):
#   results.tsv         one row per unit: label  profile  filter  n  exit  wall_s  status  jsonl  ndjson_lines
#   <LABEL>.jsonl       the run's bench_report_<pid>.jsonl (full per-cell metrics)
#   <LABEL>.log         stdout/err tail (gtest result + any crash/assert)
#   <LABEL>.tracelog    path pointer to the PID-dir test_integration.log (rich WSUPLOAD_TRACE)
#   <LABEL>.DONE        sentinel (resume marker)
#   runner.log          narrator
set -uo pipefail

QUEUE="${1:?queue file}"; COLLECT="${2:?collect dir}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NETEM="${SCRIPT_DIR}/netem_profile.sh"
USERAGENT="${USERAGENT:-JenkinsCanSpam-SDK}"
mkdir -p "$COLLECT"
RESULTS="$COLLECT/results.tsv"
[[ -f "$RESULTS" ]] || printf 'label\tprofile\tfilter\tn\texit\twall_s\tstatus\tjsonl\tlines\n' > "$RESULTS"
RUNLOG="$COLLECT/runner.log"
say(){ echo "[$(date -u +%H:%M:%SZ)] $*" | tee -a "$RUNLOG"; }

say "RUNNER START queue=$QUEUE collect=$COLLECT"
# HR56 guard: refuse to start if another test_integration is already running.
# NOTE: the binary's comm is truncated to 15 chars ("test_integratio"), so -x fails;
# match the full command line instead.
if pgrep -f 'tests/integration/test_integration' >/dev/null 2>&1; then
  say "ABORT: a test_integration is already running (HR56). Refusing to start a second."; exit 3
fi

total=$(grep -cvE '^\s*(#|$)' "$QUEUE"); idx=0
while IFS='|' read -r LABEL BINDIR PROFILE FILTER N TIMEOUT_S EXTRA_ENV; do
  [[ -z "${LABEL// }" || "${LABEL:0:1}" == "#" ]] && continue
  idx=$((idx+1))
  LABEL="${LABEL// }"; BINDIR="${BINDIR// }"; PROFILE="${PROFILE// }"; N="${N// }"; TIMEOUT_S="${TIMEOUT_S// }"
  if [[ -f "$COLLECT/$LABEL.DONE" ]]; then say "[$idx/$total] SKIP $LABEL (DONE exists)"; continue; fi
  BIN="$BINDIR/tests/integration/test_integration"
  if [[ ! -x "$BIN" ]]; then say "[$idx/$total] FAIL $LABEL: binary missing $BIN"; printf '%s\t%s\t%s\t%s\t-\t-\tNO_BINARY\t-\t0\n' "$LABEL" "$PROFILE" "$FILTER" "$N" >>"$RESULTS"; touch "$COLLECT/$LABEL.DONE"; continue; fi

  # Fail-fast on missing creds: a missing environment2.txt would make every unit
  # fail instantly (rc=255) and the loop would otherwise mark them all DONE, burning
  # the queue. Abort the whole runner instead so the queue can be re-run after fixing.
  if [[ ! -f "$BINDIR/tests/integration/environment2.txt" ]]; then
    say "FATAL: environment2.txt missing in $BINDIR/tests/integration — aborting (queue NOT burned). Copy it from a working build dir and re-run."
    exit 4
  fi
  ( set -a; source "$BINDIR/tests/integration/environment2.txt"; set +a; [[ -n "${MEGA_EMAIL:-}" ]] ) || { say "FATAL: MEGA_EMAIL not set after sourcing $BINDIR/.../environment2.txt — aborting."; exit 4; }
  say "[$idx/$total] RUN $LABEL  profile=$PROFILE filter=$FILTER n=$N timeout=${TIMEOUT_S}s"
  t0=$(date +%s)
  (
    cd "$BINDIR/tests/integration" || exit 9
    set -a; source ./environment2.txt; set +a
    # shellcheck disable=SC2086
    timeout "$TIMEOUT_S" "$NETEM" --profile "$PROFILE" -- \
      env ${EXTRA_ENV:-} stdbuf -oL -eL ./test_integration --CI --COUT --USERAGENT:"$USERAGENT" \
      --gtest_filter="$FILTER" --gtest_repeat="$N"
  ) > "$COLLECT/$LABEL.fullout.log" 2>&1
  rc=$?; t1=$(date +%s); wall=$((t1-t0))

  # classify
  status=OTHER
  case $rc in
    0) status=PASS;; 1) status=GTEST_FAIL;; 124) status=TIMEOUT_STALL;;
    134) status=SIGABRT_ASSERT;; 139) status=SIGSEGV; ;; 137) status=SIGKILL;; *) status="RC_$rc";;
  esac
  if grep -qaiE 'ERROR: AddressSanitizer|heap-use-after-free|heap-buffer-overflow' "$COLLECT/$LABEL.fullout.log"; then status="ASAN_${status}"; fi

  # collect newest bench jsonl + trace log dir produced by this run (by mtime, last 10 min)
  jsonl=$(find "$HOME/mega_tests" -name 'bench_report_*.jsonl' -newermt "@$t0" 2>/dev/null | xargs -r ls -t 2>/dev/null | head -1)
  lines=0
  if [[ -n "$jsonl" && -f "$jsonl" ]]; then cp -a "$jsonl" "$COLLECT/$LABEL.jsonl"; lines=$(grep -c '' "$COLLECT/$LABEL.jsonl"); fi
  tracelog=$(find "$HOME/mega_tests" -name 'test_integration.log' -newermt "@$t0" 2>/dev/null | xargs -r ls -t 2>/dev/null | head -1)
  [[ -n "$tracelog" ]] && echo "$tracelog" > "$COLLECT/$LABEL.tracelog"
  # keep a compact result-line tail (not the whole verbose log)
  { grep -aE '\[  (PASSED|FAILED)|\[       OK|\[  FAILED|\(.*ms\)|Bench report|AddressSanitizer|SIGSEGV|Assertion|in-flight chunks' "$COLLECT/$LABEL.fullout.log" | tail -25; } > "$COLLECT/$LABEL.log" 2>/dev/null
  rm -f "$COLLECT/$LABEL.fullout.log"

  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$LABEL" "$PROFILE" "$FILTER" "$N" "$rc" "$wall" "$status" "${jsonl:-none}" "$lines" >> "$RESULTS"
  say "[$idx/$total] DONE  $LABEL  rc=$rc wall=${wall}s status=$status jsonl_lines=$lines"
  touch "$COLLECT/$LABEL.DONE"
done < "$QUEUE"

say "RUNNER COMPLETE: $(grep -cvE '^\s*(#|$)' "$QUEUE") units; results -> $RESULTS"
touch "$COLLECT/RUNNER.ALLDONE"
