#!/usr/bin/env bash
# Scan ${MEGA_TESTS_ROOT:-${HOME}/mega_tests}/pid_* for bench_report_*.json
# and print a markdown table sorted by mtime.
#
# Columns: pid_dir | mtime | size_bytes | cell_count
# The cell_count is parsed via python3 from the JSON (len of "cells").
#
# Usage:
#   scripts/ci/find_bench_reports.sh
#   scripts/ci/find_bench_reports.sh --help
#
# Override the scan root with MEGA_TESTS_ROOT.

set -euo pipefail

usage() {
    cat <<'EOF'
usage: find_bench_reports.sh [--help]

Scans ${MEGA_TESTS_ROOT:-${HOME}/mega_tests}/pid_*/ for bench_report_*.json
files and prints a markdown table sorted by modification time.

Up to the 30 most-recent reports are listed.
EOF
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
esac

ROOT="${MEGA_TESTS_ROOT:-${HOME}/mega_tests}"

echo "| pid_dir | mtime | size_bytes | cell_count |"
echo "|---|---|---|---|"

if [ ! -d "$ROOT" ]; then
    echo "_(scan root \`$ROOT\` does not exist)_"
    exit 0
fi

# -printf '%T@\t%p\n' is GNU find; fall back to a portable mtime sort if absent.
matches=$(find "$ROOT" -maxdepth 2 -name 'bench_report_*.json' \
    -printf '%T@\t%p\n' 2>/dev/null | sort -n | tail -30 | cut -f2 || true)

if [ -z "$matches" ]; then
    echo "_(no bench_report_*.json found under \`$ROOT\`)_"
    exit 0
fi

while IFS= read -r f; do
    [ -n "$f" ] || continue
    pid_dir=$(dirname "$f")
    size=$(stat -c '%s' "$f" 2>/dev/null || echo "?")
    mtime=$(stat -c '%y' "$f" 2>/dev/null | cut -d. -f1)
    count=$(python3 -c "import json,sys
try:
    print(len(json.load(open(sys.argv[1]))['cells']))
except Exception:
    print('?')" "$f" 2>/dev/null || echo "?")
    echo "| $(basename "$pid_dir") | $mtime | $size | $count |"
done <<< "$matches"
