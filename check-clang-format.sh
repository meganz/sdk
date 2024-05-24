OUTPUT=$(git -c color.ui=always clang-format HEAD~1 --diff)

printf '%s\n' "$OUTPUT"

if ! echo $OUTPUT | grep -q 'no modified files to format'
then
    exit 1
fi
