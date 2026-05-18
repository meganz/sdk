#!/usr/bin/env bash
# Diff two TSAN log archives by race signature.
#
# Usage:
#   scripts/ci/compare_tsan.sh A.log[.gz] B.log[.gz]
#
# A signature is the first frame after each
#   "WARNING: ThreadSanitizer: <race kind>"
# block (typically the "#0 ..." line). Lines printed:
#   - total race count per file
#   - unique signature count per file
#   - signatures NEW in B (present in B but not A)
#   - signatures ABSENT in B (present in A but not B)
#
# Stdout is markdown. Exits non-zero on bad arguments or missing files.

set -euo pipefail

usage() {
    cat <<EOF >&2
usage: compare_tsan.sh A.log[.gz] B.log[.gz]

Diff two TSAN log archives. Outputs markdown summary.
EOF
}

if [ "$#" -ne 2 ]; then
    usage
    exit 2
fi

A="$1"
B="$2"

for f in "$A" "$B"; do
    [ -r "$f" ] || { echo "error: cannot read: $f" >&2; exit 2; }
done

# Read .gz or plain .log into stdout.
read_log() {
    local path="$1"
    case "$path" in
        *.gz) gunzip -c "$path" ;;
        *)    cat "$path" ;;
    esac
}

# Extract one signature per "WARNING: ThreadSanitizer:" block.
# Pull the first source-bearing frame after the warning header.
extract_sigs() {
    local path="$1"
    read_log "$path" | awk '
        /WARNING: ThreadSanitizer:/ { in_block = 1; sig = ""; next }
        in_block && /^[[:space:]]*#0 / {
            line = $0
            sub(/^[[:space:]]*#0[[:space:]]+/, "", line)
            # Trim trailing whitespace/CR.
            sub(/[[:space:]]+$/, "", line)
            print line
            in_block = 0
        }
        in_block && /^==================/ {
            # End of block with no frame seen.
            in_block = 0
        }
    ' | sort -u
}

count_races() {
    read_log "$1" | grep -c 'WARNING: ThreadSanitizer:' || true
}

TMPDIR_=$(mktemp -d)
trap 'rm -rf "$TMPDIR_"' EXIT

A_SIGS="$TMPDIR_/a.sigs"
B_SIGS="$TMPDIR_/b.sigs"

extract_sigs "$A" > "$A_SIGS"
extract_sigs "$B" > "$B_SIGS"

A_RACES=$(count_races "$A")
B_RACES=$(count_races "$B")
A_UNIQ=$(wc -l < "$A_SIGS" | tr -d ' ')
B_UNIQ=$(wc -l < "$B_SIGS" | tr -d ' ')

echo "# TSAN comparison: A=\`$(basename "$A")\` vs B=\`$(basename "$B")\`"
echo
echo "| file | total race warnings | unique signatures |"
echo "|---|---|---|"
echo "| A | $A_RACES | $A_UNIQ |"
echo "| B | $B_RACES | $B_UNIQ |"
echo

NEW_B=$(comm -13 "$A_SIGS" "$B_SIGS" || true)
ABSENT_B=$(comm -23 "$A_SIGS" "$B_SIGS" || true)

echo "## NEW in B (regressions)"
echo
if [ -z "$NEW_B" ]; then
    echo "_(none)_"
else
    echo "$NEW_B" | sed 's/^/- /'
fi
echo

echo "## ABSENT in B (fixed since A)"
echo
if [ -z "$ABSENT_B" ]; then
    echo "_(none)_"
else
    echo "$ABSENT_B" | sed 's/^/- /'
fi
echo
