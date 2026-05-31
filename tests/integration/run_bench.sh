#!/usr/bin/env bash
# Wrapper for running SDK benchmark cells against the ON binary.
# The default dev-unix-wsupload build has MEGA_BENCH_FRAMEWORK_ENABLED=OFF, so
# bench_report_<PID>.{json,jsonl} are never emitted from it. Use this script (or
# the dev-unix-wsupload-benchOn preset directly) for any benchmarking work.
# Artifacts land under pid_<PID>/bench_reports/.
#
# Usage:
#   bash tests/integration/run_bench.sh '<gtest_filter>' [repeat]
#
# Overrides:
#   BENCH_BUILD_DIR  path to a build dir compiled with MEGA_BENCH_FRAMEWORK_ENABLED=ON
#                    (default: $HOME/repo/build-sdk-dev-unix-wsupload-benchOn)
#   USERAGENT        --USERAGENT value (default: JenkinsCanSpam-SDK)

set -euo pipefail

BENCH_BUILD_DIR="${BENCH_BUILD_DIR:-${HOME}/repo/build-sdk-dev-unix-wsupload-benchOn}"
USERAGENT="${USERAGENT:-JenkinsCanSpam-SDK}"
BIN="${BENCH_BUILD_DIR}/tests/integration/test_integration"

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 '<gtest_filter>' [repeat]" >&2
    exit 2
fi

if [[ ! -x "${BIN}" ]]; then
    echo "ERROR: bench binary not found at ${BIN}" >&2
    echo "Build with:" >&2
    echo "  cmake --preset dev-unix-wsupload-benchOn" >&2
    echo "  cmake --build ${BENCH_BUILD_DIR} -j16 --target=test_integration" >&2
    exit 1
fi

# Sanity: bench cells only register when MEGA_BENCH_FRAMEWORK_ENABLED=ON.
if ! "${BIN}" --gtest_list_tests 2>/dev/null | grep -q '^SdkBenchmark'; then
    echo "ERROR: bench binary at ${BIN} does not expose SdkBenchmark* cells." >&2
    echo "Confirm it was built with -DMEGA_BENCH_FRAMEWORK_ENABLED=ON." >&2
    exit 1
fi

filter="$1"
repeat="${2:-1}"

cd "$(dirname "${BIN}")"

if [[ -f environment2.txt ]]; then
    set -a
    # shellcheck disable=SC1091
    source ./environment2.txt
    set +a
fi

"${BIN}" \
    --CI --COUT \
    --USERAGENT:"${USERAGENT}" \
    --gtest_filter="${filter}" \
    --gtest_repeat="${repeat}"

echo
echo "Latest bench_reports:"
latest_json=$(ls -t "${HOME}/mega_tests/pid_"*"/bench_reports/bench_report_"*".json" 2>/dev/null | head -1 || true)
latest_jsonl=$(ls -t "${HOME}/mega_tests/pid_"*"/bench_reports/bench_report_"*".jsonl" 2>/dev/null | head -1 || true)
if [[ -n "${latest_json}" ]]; then
    echo "  json:  ${latest_json}"
fi
if [[ -n "${latest_jsonl}" ]]; then
    echo "  jsonl: ${latest_jsonl}"
fi
if [[ -z "${latest_json}" && -z "${latest_jsonl}" ]]; then
    echo "  (none found — verify MEGA_BENCH_FRAMEWORK_ENABLED=ON and that bench cells were exercised)"
fi
