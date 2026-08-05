#!/bin/bash
# ci_tier1_gate.sh — HR57/HR58 CI-faithful local gate (SDK-5360 fu8 S11).
#
# Runs the WS-upload Tier-1 surface the way Jenkins MR CI runs it — DEFAULT env (no
# MEGA_WS_*/MEGA_BENCH_* knobs), --CI, both the direct and the GTestParallelRunner
# (--INSTANCES) invocation forms — and fails on any FAILED test or any fabricated/mislabeled
# skip verdict. This is the committed, diffable definition of the local pre-push gate; the
# 93360b994d all-platform CI fire escaped because no local sweep ever ran this form
# (see followup8_QA_10/audit/SWEEP_ESCAPE_POSTMORTEM.md).
#
# Usage: ci_tier1_gate.sh <build_dir> [out_dir]
#   build_dir: CMake build tree containing tests/integration/test_integration and
#              environment2.txt (copied from a KEEP dir; builds do not produce it).
#   out_dir:   where to write logs + verdict (default: ./ci_tier1_gate_<timestamp>).
#
# Rules enforced (BENCHMARKS.md "Hard rules"):
#   HR58 — every cell must pass or SKIP under DEFAULT env; a default-env failure is never an
#          "invocation-form artifact". Knobs are for bench cells only.
#   HR57 — after any rebase/history rewrite this gate is the MINIMUM pre-push bar (with
#          dev-unix-strict and the conflict-marker sweep).
set -u

BUILD_DIR="${1:?usage: ci_tier1_gate.sh <build_dir> [out_dir]}"
OUT_DIR="${2:-./ci_tier1_gate_$(date +%Y%m%d_%H%M%S)}"
BIN_DIR="$BUILD_DIR/tests/integration"
mkdir -p "$OUT_DIR"
FAIL=0

cd "$BIN_DIR" || { echo "GATE: no such dir $BIN_DIR"; exit 250; }
[ -x ./test_integration ] || { echo "GATE: no test_integration in $BIN_DIR"; exit 250; }
[ -f ./environment2.txt ] || { echo "GATE: missing environment2.txt (copy from a KEEP dir)"; exit 250; }
set -a; . ./environment2.txt; set +a

# HR58: scrub every bench/WS knob — Jenkins sets none.
for v in $(env | grep -oE '^(MEGA_WS_|MEGA_BENCH_|MEGA_NET_|MEGA_WSTEST_)[A-Za-z_0-9]*'); do unset "$v"; done
export WORKSPACE="${WORKSPACE:-$HOME/mega_tests}"

UA="--USERAGENT:JenkinsCanSpam-SDK"
{
  echo "gate=ci_tier1_gate  date=$(date -Is)"
  echo "binary=$BIN_DIR/test_integration md5=$(md5sum ./test_integration | cut -d' ' -f1)"
  echo "gitHEAD=$(git -C "$(dirname "$0")/../.." rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
  echo "scrubbed_env_leftover=$(env | grep -cE '^(MEGA_WS_|MEGA_BENCH_|MEGA_NET_)' || true)"
} > "$OUT_DIR/provenance.txt"

run_cell() { # label timeout_s args...
  local label="$1" tmo="$2"; shift 2
  echo "GATE: [$label] $*"
  timeout "$tmo" ./test_integration --CI "$UA" "$@" > "$OUT_DIR/$label.log" 2>&1
  local rc=$?
  echo "rc=$rc" > "$OUT_DIR/$label.rc"
  # A skip-ending run must exit 0; CRASHED must never appear (fabricated-verdict guard);
  # GTestLogger must never label a kSkip part "Failure".
  if grep -qE "\] .* CRASHED" "$OUT_DIR/$label.log"; then
    echo "GATE: [$label] FABRICATED-CRASH VERDICT DETECTED"; FAIL=1
  fi
  if [ "$rc" -ne 0 ]; then
    echo "GATE: [$label] rc=$rc"; FAIL=1
  fi
  return 0
}

# Cell 1 — the full WS Tier-1 surface, DIRECT form, DEFAULT env (the form CI's main stage
# uses per test; empty-filter-equivalent for the WS surface).
run_cell tier1_ws_direct 5400 "--gtest_filter=SdkWsUploadTest.*"

# Cell 2 — the skip-prone cells under the PARALLEL runner (the form that fabricated CRASHED
# verdicts pre-F1). Two workers exercise GTestProc line-parsing + tally.
run_cell skiplabel_instances 1200 --INSTANCES:2 \
  "--gtest_filter=SdkWsUploadTest.EscalationWindowNotPerpetuallyResetByNullCandidate:SdkBenchmarkTest.QaMixedUpload"

# Cell 3 — bench/dataset cells under DEFAULT env: must SKIP cleanly, never crash/fail.
run_cell bench_default_env 900 \
  "--gtest_filter=SdkBenchmarkTest.QaMixedUpload:SdkTest.HarvestQaMixedDataset"

# Cell 4 — win-parity: Harvest with NO HOME must SKIP (USERPROFILE also absent on linux).
echo "GATE: [harvest_nohome] env -u HOME"
env -u HOME timeout 900 ./test_integration --CI "$UA" \
  "--gtest_filter=SdkTest.HarvestQaMixedDataset" > "$OUT_DIR/harvest_nohome.log" 2>&1
rc=$?
echo "rc=$rc" > "$OUT_DIR/harvest_nohome.rc"
if [ "$rc" -ne 0 ] || ! grep -q "\[  SKIPPED \]" "$OUT_DIR/harvest_nohome.log"; then
  echo "GATE: [harvest_nohome] expected clean SKIP, rc=$rc"; FAIL=1
fi

# Cell 5 — S13 round-3 (Cluster F postmortem): the CI-FAITHFUL provisioned form. Cells 3/4
# only ever asserted the SKIP form, so the gate was structurally blind to the form CI
# actually runs (MEGA_BENCH_UPLOAD_SOURCE_DIR exported, dataset on a persistent agent
# path) — which is exactly where round 3 failed on linux+win. Runs QaMixedUpload against
# the local harvested dataset; the in-test manifest guard catches dataset drift.
QA_DATASET_DIR="${HOME:?}/mega_bench_dataset/qa_mixed"
if [ -d "$QA_DATASET_DIR" ]; then
  echo "GATE: [bench_ci_env] MEGA_BENCH_UPLOAD_SOURCE_DIR=$QA_DATASET_DIR"
  env MEGA_BENCH_UPLOAD_SOURCE_DIR="$QA_DATASET_DIR" timeout 1800 ./test_integration --CI "$UA" \
    "--gtest_filter=SdkBenchmarkTest.QaMixedUpload" > "$OUT_DIR/bench_ci_env.log" 2>&1
  rc=$?
  echo "rc=$rc" > "$OUT_DIR/bench_ci_env.rc"
  if grep -qE "\] .* CRASHED" "$OUT_DIR/bench_ci_env.log"; then
    echo "GATE: [bench_ci_env] FABRICATED-CRASH VERDICT DETECTED"; FAIL=1
  fi
  if [ "$rc" -ne 0 ]; then
    echo "GATE: [bench_ci_env] rc=$rc"; FAIL=1
  fi
else
  echo "GATE: [bench_ci_env] DATASET MISSING at $QA_DATASET_DIR — run" \
       "SdkTest.HarvestQaMixedDataset once on this host; the CI-faithful form is REQUIRED"
  FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then
  echo "GATE: PASS" | tee "$OUT_DIR/VERDICT.txt"
else
  echo "GATE: FAIL — see $OUT_DIR" | tee "$OUT_DIR/VERDICT.txt"
fi
exit "$FAIL"
