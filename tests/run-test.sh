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

US=$(printf '\037')
TMP_OUT=

cleanup() {
    [ -n "$TMP_OUT" ] && rm -f "$TMP_OUT"
}
trap cleanup EXIT HUP INT TERM

# Checks every non-comment line of the known-gap file. Prints one
# "MALFORMED KNOWN GAP: line <n>: <reason>" line per problem and returns 1 if
# there was any.
validate_known_gaps() {
    problems=$(awk -F'|' -v programs="$ECS_TEST_PROGRAMS" '
        function trim(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); return s }
        function bad(reason) {
            printf "MALFORMED KNOWN GAP: line %d: %s\n", NR, reason
        }
        BEGIN { n = split(programs, list, /[ \t]+/); for (i = 1; i <= n; i++) known[list[i]] = 1 }
        /^[ \t]*$/ { next }
        /^#/ { next }
        {
            if (NF != 6) { bad("expected 6 fields separated by \" | \", found " NF); next }
            for (i = 1; i <= 6; i++) f[i] = trim($i)
            ok = 1
            if (f[1] != "address" && f[1] != "thread") { bad("variant must be address or thread"); ok = 0 }
            if (!(f[2] in known)) { bad("program is not one of the test programs: " f[2]); ok = 0 }
            if (f[3] == "") { bad("test case is empty"); ok = 0 }
            if (f[4] == "" || f[4] == "AddressSanitizer" || f[4] == "ThreadSanitizer" ||
                f[4] == "UndefinedBehaviorSanitizer" || f[4] == "runtime error") {
                bad("signature is empty or only a sanitizer name"); ok = 0
            }
            if (f[5] !~ /^[^ ]+ finding [0-9]+$/) { bad("finding must look like <review> finding <N>"); ok = 0 }
            if (f[6] !~ /^[^ :]+: ./) { bad("fixed by must look like <roadmap>: <task title>"); ok = 0 }
            key = f[1] SUBSEP f[2] SUBSEP f[3]
            if (ok && (key in seen)) { bad("duplicate entry for " f[1] " " f[2] " \"" f[3] "\" (first on line " seen[key] ")"); ok = 0 }
            if (!(key in seen)) seen[key] = NR
        }
    ' "$ECS_KNOWN_GAPS_FILE")
    if [ -n "$problems" ]; then
        printf '%s\n' "$problems"
        return 1
    fi
    return 0
}

# Prints the entries for this variant and program, one per line, with the
# fields (test case, signature, finding, fixed by) separated by \037.
select_entries() {
    awk -F'|' -v variant="$ECS_SANITIZER" -v program="$P" -v us="$US" '
        function trim(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); return s }
        /^[ \t]*$/ { next }
        /^#/ { next }
        trim($1) == variant && trim($2) == program {
            printf "%s%s%s%s%s%s%s\n", trim($3), us, trim($4), us, trim($5), us, trim($6)
        }
    ' "$ECS_KNOWN_GAPS_FILE"
}

# Runs a program that has known-gap entries: everything except the listed
# cases first, then each listed case on its own.
run_with_entries() {
    prog=$1
    entries=$2
    result=0

    negations=
    while IFS=$US read -r tc sig finding fixed; do
        negations="$negations ~\"$tc\""
    done <<ENTRIES_EOF
$entries
ENTRIES_EOF
    negations=${negations# }

    if ! "$prog" --allow-running-no-tests "$negations"; then
        echo "FAIL: non-listed test cases failed"
        result=1
    fi

    TMP_OUT=$(mktemp "${TMPDIR:-/tmp}/run-test.XXXXXX") || return 99
    while IFS=$US read -r tc sig finding fixed; do
        "$prog" "\"$tc\"" > "$TMP_OUT" 2>&1
        status=$?
        cat "$TMP_OUT"
        if grep -F -- "No test cases matched" "$TMP_OUT" > /dev/null; then
            echo "MALFORMED KNOWN GAP: test case not found: $P \"$tc\""
            result=1
        elif [ "$status" -ne 0 ] && grep -F -- "$sig" "$TMP_OUT" > /dev/null; then
            echo "KNOWN GAP: $P \"$tc\" ($sig) — $finding; fixed by $fixed"
        elif [ "$status" -ne 0 ]; then
            echo "FAIL: listed case failed without expected signature '$sig'"
            result=1
        else
            echo "STALE KNOWN GAP: $P \"$tc\" passed under $ECS_SANITIZER; remove this entry ($finding; $fixed)"
        fi
    done <<ENTRIES_EOF
$entries
ENTRIES_EOF
    return $result
}

if ! validate_known_gaps; then
    exit 1
fi

ENTRIES=$(select_entries)
if [ -z "$ENTRIES" ]; then
    exec "$1"
fi

run_with_entries "$1" "$ENTRIES"
exit $?
