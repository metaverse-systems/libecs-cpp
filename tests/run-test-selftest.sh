#!/bin/sh
# Self-test for run-test.sh. Builds fake programs that behave like a Catch2
# binary and checks the wrapper's exit status and output for each case.

srcdir=${srcdir:-.}
WRAPPER=$srcdir/run-test.sh

WORK=$(mktemp -d "${TMPDIR:-/tmp}/run-test-selftest.XXXXXX") || exit 99
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

CASE_NUM=0
FAILED=0

# make_fake <program> [<case-name>|<exit status>|<output text>]...
# Creates an executable fake Catch2 program. Accepts --allow-running-no-tests
# and one test-spec argument: a single quoted name, or space-separated
# negations ~"name". Unknown names print "No test cases matched" and exit 2
# (0 with --allow-running-no-tests). A run that executes at least one case
# exits with the first non-zero status among the executed cases.
make_fake() {
    fake_name=$1
    shift
    : > "$WORK/$fake_name.cases"
    for fake_line in "$@"; do
        printf '%s\n' "$fake_line" >> "$WORK/$fake_name.cases"
    done
    cat > "$WORK/$fake_name" <<'FAKE'
#!/bin/sh
self=$0
cases="$self.cases"
allow=no
spec=
for arg in "$@"; do
    case $arg in
        --allow-running-no-tests) allow=yes ;;
        *) spec=$arg ;;
    esac
done
names=$(printf '%s\n' "$spec" | grep -o '"[^"]*"' | sed 's/^"//; s/"$//')
selected=
if [ -z "$spec" ]; then
    selected=$(cut -d'|' -f1 "$cases")
else
    case $spec in
        '~'*)
            selected=$(cut -d'|' -f1 "$cases")
            old_ifs=$IFS
            IFS='
'
            for n in $names; do
                selected=$(printf '%s\n' "$selected" | grep -vxF -- "$n")
            done
            IFS=$old_ifs
            ;;
        *)
            for n in $names; do
                if cut -d'|' -f1 "$cases" | grep -qxF -- "$n"; then
                    selected=$n
                fi
            done
            if [ -z "$selected" ]; then
                echo "No test cases matched '$spec'"
                [ "$allow" = yes ] && exit 0
                exit 2
            fi
            ;;
    esac
fi
status=0
old_ifs=$IFS
IFS='
'
for n in $selected; do
    line=$(grep -F -- "$n|" "$cases" | head -n 1)
    code=$(printf '%s\n' "$line" | cut -d'|' -f2)
    text=$(printf '%s\n' "$line" | cut -d'|' -f3-)
    [ -n "$text" ] && echo "$text"
    if [ "${code:-0}" -ne 0 ] && [ "$status" -eq 0 ]; then
        status=$code
    fi
done
IFS=$old_ifs
if [ -z "$selected" ]; then
    echo "No tests ran"
    [ "$allow" = yes ] && exit 0
    exit 2
fi
exit "$status"
FAKE
    chmod +x "$WORK/$fake_name"
}

# run_wrapper <sanitizer> <gaps file> <programs> <fake>
# Runs the wrapper, leaving its status in STATUS and output in OUTPUT.
run_wrapper() {
    OUTPUT=$(ECS_SANITIZER=$1 ECS_KNOWN_GAPS_FILE=$2 ECS_TEST_PROGRAMS=$3 \
        "${SHELL:-/bin/sh}" "$WRAPPER" "$4" 2>&1)
    STATUS=$?
}

report() {
    if [ "$1" = ok ]; then
        echo "ok $CASE_NUM - $2"
    else
        echo "not ok $CASE_NUM - $2"
        FAILED=1
    fi
}

# expect_status <description> <expected: zero|nonzero|N>
expect_status() {
    CASE_NUM=$((CASE_NUM + 1))
    case $2 in
        zero) [ "$STATUS" -eq 0 ] && result=ok || result=bad ;;
        nonzero) [ "$STATUS" -ne 0 ] && result=ok || result=bad ;;
        *) [ "$STATUS" -eq "$2" ] && result=ok || result=bad ;;
    esac
    report "$result" "$1"
    if [ "$result" = bad ]; then
        echo "  status was $STATUS, expected $2"
        printf '%s\n' "$OUTPUT" | sed 's/^/  | /'
    fi
}

# expect_line <description> <prefix>
# Passes if some output line starts with the fixed prefix.
expect_line() {
    CASE_NUM=$((CASE_NUM + 1))
    if printf '%s\n' "$OUTPUT" | awk -v p="$2" 'index($0, p) == 1 { found = 1 } END { exit !found }'; then
        report ok "$1"
    else
        report bad "$1"
        echo "  missing line starting with: $2"
        printf '%s\n' "$OUTPUT" | sed 's/^/  | /'
    fi
}

EMPTY_GAPS=$WORK/empty-gaps.txt
printf '# variant | program | test case | signature | finding | fixed by\n' > "$EMPTY_GAPS"

# Case 1: plain variant, failing program -> non-zero
make_fake fake_fail "alpha|1|assertion failed"
run_wrapper none "$EMPTY_GAPS" fake_fail "$WORK/fake_fail"
expect_status "none with a failing program fails" nonzero

# Case 2: thread variant, no entries, passing program -> 0
make_fake fake_pass "alpha|0|fine"
run_wrapper thread "$EMPTY_GAPS" fake_pass "$WORK/fake_pass"
expect_status "thread with no entries and a passing program passes" zero

# Case 9: entries for another variant are ignored
OTHER_GAPS=$WORK/other-gaps.txt
cat > "$OTHER_GAPS" <<'GAPS'
# variant | program | test case | signature | finding | fixed by
address | fake_pass | alpha | heap-use-after-free | libecs-cpp-1 finding 1 | libecs-cpp-1: Example task
GAPS
run_wrapper thread "$OTHER_GAPS" "fake_pass fake_fail" "$WORK/fake_pass"
expect_status "entries for another variant are ignored (passing program)" zero
run_wrapper thread "$OTHER_GAPS" "fake_pass fake_fail" "$WORK/fake_fail"
expect_status "entries for another variant are ignored (failing program)" nonzero
if printf '%s\n' "$OUTPUT" | grep -q 'KNOWN GAP'; then
    CASE_NUM=$((CASE_NUM + 1))
    report bad "no known-gap lines for ignored entries"
fi

# Case 11: input errors exit 99
run_wrapper bogus "$EMPTY_GAPS" fake_pass "$WORK/fake_pass"
expect_status "unknown ECS_SANITIZER exits 99" 99
run_wrapper thread "$WORK/does-not-exist.txt" fake_pass "$WORK/fake_pass"
expect_status "missing known-gap file exits 99" 99
run_wrapper thread "$EMPTY_GAPS" "" "$WORK/fake_pass"
expect_status "empty ECS_TEST_PROGRAMS exits 99" 99

# write_gaps <file> [<entry line>]...
# Writes a known-gap file with the standard header followed by the given lines.
write_gaps() {
    gaps_file=$1
    shift
    printf '# variant | program | test case | signature | finding | fixed by\n' > "$gaps_file"
    for gaps_line in "$@"; do
        printf '%s\n' "$gaps_line" >> "$gaps_file"
    done
}

RACE='WARNING: ThreadSanitizer: data race'
TAIL='libecs-cpp-1 finding 3 | libecs-cpp-1: Example task'

# Case 3: entry hit with a matching signature, other cases pass -> 0 and KNOWN GAP
make_fake fake_k "alpha|0|fine" "beta|1|$RACE"
write_gaps "$WORK/hit.txt" "thread | fake_k | beta | data race | $TAIL"
run_wrapper thread "$WORK/hit.txt" fake_k "$WORK/fake_k"
expect_status "listed case with matching signature passes" zero
expect_line "listed case with matching signature reports KNOWN GAP" "KNOWN GAP:"

# Case 4: listed case fails with a different signature -> FAIL
make_fake fake_k "alpha|0|fine" "beta|1|heap-use-after-free"
run_wrapper thread "$WORK/hit.txt" fake_k "$WORK/fake_k"
expect_status "listed case with a different signature fails" nonzero
expect_line "listed case with a different signature reports FAIL" \
    "FAIL: listed case failed without expected signature"

# Case 5: a non-listed case fails while the listed one hits -> FAIL
make_fake fake_k "alpha|1|assertion failed" "beta|1|$RACE"
run_wrapper thread "$WORK/hit.txt" fake_k "$WORK/fake_k"
expect_status "failing non-listed case fails" nonzero
expect_line "failing non-listed case reports FAIL" "FAIL: non-listed test cases failed"

# Case 6: listed case passes -> 0 and STALE KNOWN GAP
make_fake fake_k "alpha|0|fine" "beta|0|fine"
run_wrapper thread "$WORK/hit.txt" fake_k "$WORK/fake_k"
expect_status "listed case that passes still passes" zero
expect_line "listed case that passes reports STALE KNOWN GAP" "STALE KNOWN GAP:"

# Case 7: malformed entries -> non-zero and MALFORMED KNOWN GAP, each on its own
make_fake fake_k "alpha|0|fine" "beta|1|$RACE"
make_fake fake_other "alpha|0|fine"
GOOD="thread | fake_k | beta | data race | $TAIL"
PROGS="fake_k fake_other"

write_gaps "$WORK/bad.txt" "thread | fake_k | beta | data race | libecs-cpp-1 finding 3"
run_wrapper thread "$WORK/bad.txt" "$PROGS" "$WORK/fake_k"
expect_status "entry with five fields fails" nonzero
expect_line "entry with five fields reports MALFORMED KNOWN GAP" "MALFORMED KNOWN GAP:"

write_gaps "$WORK/bad.txt" "plain | fake_k | beta | data race | $TAIL"
run_wrapper thread "$WORK/bad.txt" "$PROGS" "$WORK/fake_k"
expect_status "entry with the plain variant fails" nonzero
expect_line "entry with the plain variant reports MALFORMED KNOWN GAP" "MALFORMED KNOWN GAP:"

write_gaps "$WORK/bad.txt" "thread | fake_k | beta | ThreadSanitizer | $TAIL"
run_wrapper thread "$WORK/bad.txt" "$PROGS" "$WORK/fake_k"
expect_status "entry with a bare sanitizer name as signature fails" nonzero
expect_line "entry with a bare sanitizer name reports MALFORMED KNOWN GAP" "MALFORMED KNOWN GAP:"

write_gaps "$WORK/bad.txt" "$GOOD" "$GOOD"
run_wrapper thread "$WORK/bad.txt" "$PROGS" "$WORK/fake_k"
expect_status "duplicate entry fails" nonzero
expect_line "duplicate entry reports MALFORMED KNOWN GAP" "MALFORMED KNOWN GAP:"

write_gaps "$WORK/bad.txt" "thread | fake_missing | beta | data race | $TAIL"
run_wrapper thread "$WORK/bad.txt" "$PROGS" "$WORK/fake_k"
expect_status "entry for a program not in ECS_TEST_PROGRAMS fails" nonzero
expect_line "entry for an unlisted program reports MALFORMED KNOWN GAP" "MALFORMED KNOWN GAP:"

# A malformed line that names a different program still fails the program under test.
write_gaps "$WORK/bad.txt" "$GOOD" "thread | fake_other | alpha | data race | libecs-cpp-1 finding 3"
run_wrapper thread "$WORK/bad.txt" "$PROGS" "$WORK/fake_k"
expect_status "malformed entry for another program fails this program" nonzero
expect_line "malformed entry for another program reports MALFORMED KNOWN GAP" "MALFORMED KNOWN GAP:"

# Case 8: unknown test case -> MALFORMED KNOWN GAP: test case not found
write_gaps "$WORK/unknown.txt" "thread | fake_k | no such case | data race | $TAIL"
run_wrapper thread "$WORK/unknown.txt" fake_k "$WORK/fake_k"
expect_status "entry for an unknown test case fails" nonzero
expect_line "entry for an unknown test case reports test case not found" \
    "MALFORMED KNOWN GAP: test case not found"

# Case 10: every test case of the program is listed and each one hits -> 0
make_fake fake_all "beta|1|$RACE" "gamma|1|$RACE"
write_gaps "$WORK/all.txt" \
    "thread | fake_all | beta | data race | $TAIL" \
    "thread | fake_all | gamma | data race | $TAIL"
run_wrapper thread "$WORK/all.txt" fake_all "$WORK/fake_all"
expect_status "program with every case listed and hitting passes" zero
expect_line "program with every case listed reports KNOWN GAP" "KNOWN GAP:"

# Further cases are appended above this line.

if [ "$FAILED" -ne 0 ]; then
    echo "run-test-selftest: FAILED"
    exit 1
fi
echo "run-test-selftest: all $CASE_NUM checks passed"
exit 0
