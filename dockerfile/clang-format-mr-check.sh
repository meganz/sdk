#!/usr/bin/env bash

set -u

TARGET_REF="${1:-}"
SOURCE_REF="${2:-}"

if [[ -z "$TARGET_REF" || -z "$SOURCE_REF" ]]; then
  echo "Usage: $(basename "$0") <target-ref> <source-ref>" >&2
  echo "Example: $(basename "$0") origin/develop origin/feature" >&2
  exit 2
fi

COMMITS=$(git log --pretty=format:%H "${TARGET_REF}..${SOURCE_REF}" || true)
FOUND_ISSUES=0
BAD_COMMITS=()

echo "    ######################"
echo "    # ClangFormat output #"
echo "    ######################"
echo ""
echo "    Range: ${TARGET_REF}..${SOURCE_REF}"
echo "    Commits: ${COMMITS}"
echo ""

if [[ -z "$COMMITS" ]]; then
  echo "    No commits to check."
  echo ""
  echo "    #############################"
  echo "    # End of ClangFormat output #"
  echo "    #############################"
  exit 0
fi

for COMMIT in $COMMITS
do
  OUTPUT=$(git -c color.ui=always clang-format --diff \
    --extensions "c,c++,c++m,cc,ccm,cp,cpp,cppm,cxx,cxxm,h,hh,hpp,hxx" \
    "$COMMIT^" "$COMMIT" || true)

  echo "    Commit: ${COMMIT}"

  if [[ -z "$OUTPUT" ]]; then
    echo "    No ClangFormat issues found."
    continue
  fi

  if grep -q 'no modified files to format' <<<"$OUTPUT"; then
    echo "    ${OUTPUT}"
    continue
  fi

  if grep -q 'clang-format did not modify any files' <<<"$OUTPUT"; then
    echo "    ${OUTPUT}"
    continue
  fi

  echo "    ClangFormat:"
  printf '%s\n' "$OUTPUT"
  echo ""
  FOUND_ISSUES=1
  BAD_COMMITS+=("$COMMIT")
done

if [[ $FOUND_ISSUES -ne 0 ]]; then
  echo "    ############################"
  echo "    # ClangFormat issues found #"
  echo "    ############################"
  echo ""
  echo "    Commits with diffs: ${BAD_COMMITS[*]}"
  echo ""
  exit 1
fi

echo "    #############################"
echo "    # End of ClangFormat output #"
echo "    #############################"
