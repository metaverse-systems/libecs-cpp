#!/bin/sh
# Test wrapper used by "make check" as the LOG_COMPILER for every test program.
#
# Usage: run-test.sh <test program>
#
# Environment:
#   ECS_SANITIZER        none, address or thread
#   ECS_KNOWN_GAPS_FILE  path to the known-gap list
#   ECS_TEST_PROGRAMS    space-separated names of all test programs
#
# Exit status: the program's own status, 1 for a wrapper-detected failure,
# 99 for unusable inputs.

P=$(basename "$1")

case ${ECS_SANITIZER:-} in
    none|address|thread) ;;
    *)
        echo "run-test.sh: ECS_SANITIZER must be none, address or thread (got '${ECS_SANITIZER:-}')" >&2
        exit 99
        ;;
esac

if [ -z "${ECS_KNOWN_GAPS_FILE:-}" ] || [ ! -f "$ECS_KNOWN_GAPS_FILE" ]; then
    echo "run-test.sh: known-gap file not found: '${ECS_KNOWN_GAPS_FILE:-}'" >&2
    exit 99
fi

if [ -z "${ECS_TEST_PROGRAMS:-}" ]; then
    echo "run-test.sh: ECS_TEST_PROGRAMS is empty" >&2
    exit 99
fi

if [ "$ECS_SANITIZER" = none ]; then
    exec "$1"
fi

# Checks every entry of the known-gap file. Not implemented yet.
validate_known_gaps() {
    return 0
}

# Prints the entries for this variant and program, one per line.
select_entries() {
    while IFS= read -r line || [ -n "$line" ]; do
        case $line in
            ''|'#'*) continue ;;
        esac
        variant=$(printf '%s\n' "$line" | awk -F'|' '{ gsub(/^[ \t]+|[ \t]+$/, "", $1); print $1 }')
        program=$(printf '%s\n' "$line" | awk -F'|' '{ gsub(/^[ \t]+|[ \t]+$/, "", $2); print $2 }')
        if [ "$variant" = "$ECS_SANITIZER" ] && [ "$program" = "$P" ]; then
            printf '%s\n' "$line"
        fi
    done < "$ECS_KNOWN_GAPS_FILE"
}

# Handles a program that has known-gap entries. Not implemented yet: it always
# fails so that an entry can never silently pass.
run_with_entries() {
    echo "FAIL: known-gap entries are not handled yet"
    return 1
}

validate_known_gaps

ENTRIES=$(select_entries)
if [ -z "$ENTRIES" ]; then
    exec "$1"
fi

run_with_entries "$1" "$ENTRIES"
exit $?
