#!/bin/sh
# Logging checks that run outside the unit tests.
#
# Stanza 1: the library writes to the console only inside the default destination. The region between
# the lines "// console-output: begin" and "// console-output: end" (in any file of src/) is the default
# destination and is ignored, as are include/libecs-cpp/json.hpp and src/example.cpp. Any other
# std::cout, std::cerr, printf, puts, fputs or write to stdout or stderr in src/ or include/libecs-cpp/
# fails the stanza and each offending line is printed.
#
# Stanzas 2 to 8: the default destination writes no escape sequence when the streams are not a terminal.
# The helper program log_probe logs five lines (debug, info, warning, error and an unknown severity) and
# exits 0; the script redirects its standard output and standard error in several ways and looks for the
# escape character. Stanzas 9 and 10 run it on a pseudo-terminal (script or python3 pty) and expect colour,
# and no colour with NO_COLOR=1; they are skipped, and the script still passes, when neither tool exists.

root="$(cd "$(dirname "$0")/.." && pwd)"
status=0

# Prints "file:line:text" for every line outside a marked region that writes to the console.
console_writes()
{
    for file in "$root"/src/* "$root"/include/libecs-cpp/*; do
        [ -f "$file" ] || continue
        case "$file" in
            */include/libecs-cpp/json.hpp | */src/example.cpp) continue ;;
            *.cpp | *.hpp | *.h | *.cc) ;;
            *) continue ;;
        esac
        awk -v name="${file#"$root"/}" '
            /\/\/ console-output: begin/ { skip = 1; next }
            /\/\/ console-output: end/ { skip = 0; next }
            skip { next }
            /std::cout|std::cerr|printf|(^|[^A-Za-z0-9_])(puts|fputs)[ \t]*\(|(^|[^A-Za-z0-9_])(stdout|stderr)([^A-Za-z0-9_]|$)/ {
                print name ":" NR ":" $0
            }
        ' "$file"
    done
}

offending="$(console_writes)"
if [ -n "$offending" ]; then
    echo "FAIL: console writes outside the default destination:"
    echo "$offending"
    status=1
else
    echo "PASS: no console writes outside the default destination"
fi

probe="$(dirname "$0")/log_probe"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT INT TERM
esc="$(printf '\033')"

# Prints PASS or FAIL for a stanza and records a failure.
report()
{
    if [ "$1" = ok ]; then
        echo "PASS: $2"
    else
        echo "FAIL: $2"
        status=1
    fi
}

has_escape()
{
    grep -q "$esc" "$1"
}

if [ ! -x "$probe" ]; then
    echo "FAIL: log_probe is not built ($probe)"
    exit 1
fi

# 2: standard output to a file
"$probe" >"$work/out.txt" 2>"$work/err.txt"
code=$?
if [ "$code" -eq 0 ] && [ -s "$work/out.txt" ] && ! has_escape "$work/out.txt"; then
    report ok "standard output to a file has no escape sequence"
else
    report bad "standard output to a file has no escape sequence (exit $code)"
fi

# 3: standard error to a file
if [ -s "$work/err.txt" ] && ! has_escape "$work/err.txt"; then
    report ok "standard error to a file has no escape sequence"
else
    report bad "standard error to a file has no escape sequence"
fi

# 4: both to the same file, in the order logged
"$probe" >"$work/both.txt" 2>&1
code=$?
printf '%s\n' '[debug] probe line one' '[info] probe line two' '[warning] probe line three' \
    '[error] probe line four' '[unrecognised] probe line five' >"$work/expected.txt"
if [ "$code" -eq 0 ] && ! has_escape "$work/both.txt" && cmp -s "$work/both.txt" "$work/expected.txt"; then
    report ok "both streams to one file have no escape sequence and keep the order logged"
else
    report bad "both streams to one file have no escape sequence and keep the order logged (exit $code)"
    cat "$work/both.txt"
fi

# 5: both into a pipe
"$probe" 2>&1 | cat >"$work/pipe.txt"
if [ -s "$work/pipe.txt" ] && ! has_escape "$work/pipe.txt"; then
    report ok "both streams into a pipe have no escape sequence"
else
    report bad "both streams into a pipe have no escape sequence"
fi

# 6: standard output closed
"$probe" >&- 2>"$work/err-only.txt"
code=$?
if [ "$code" -eq 0 ] && [ -s "$work/err-only.txt" ] && ! has_escape "$work/err-only.txt"; then
    report ok "standard output closed: no crash, standard error has no escape sequence"
else
    report bad "standard output closed: no crash, standard error has no escape sequence (exit $code)"
fi

# 7: standard error closed
"$probe" 2>&- >"$work/out-only.txt"
code=$?
if [ "$code" -eq 0 ] && [ -s "$work/out-only.txt" ] && ! has_escape "$work/out-only.txt"; then
    report ok "standard error closed: no crash, standard output has no escape sequence"
else
    report bad "standard error closed: no crash, standard output has no escape sequence (exit $code)"
fi

# 8: NO_COLOR does not matter when redirected
NO_COLOR=1 "$probe" >"$work/nc.txt" 2>&1
if [ -s "$work/nc.txt" ] && ! has_escape "$work/nc.txt" && cmp -s "$work/nc.txt" "$work/expected.txt"; then
    report ok "NO_COLOR=1 with redirected streams gives the same plain text"
else
    report bad "NO_COLOR=1 with redirected streams gives the same plain text"
fi

# 9 and 10: a pseudo-terminal
run_on_terminal()
{
    # $1: output file; $2: NO_COLOR value or empty for unset
    if command -v script >/dev/null 2>&1 && script --version >/dev/null 2>&1; then
        if [ -n "$2" ]; then
            script -qec "env NO_COLOR=$2 '$probe'" /dev/null </dev/null >"$1" 2>/dev/null
        else
            script -qec "env -u NO_COLOR '$probe'" /dev/null </dev/null >"$1" 2>/dev/null
        fi
    elif command -v python3 >/dev/null 2>&1 && python3 -c 'import pty' >/dev/null 2>&1; then
        if [ -n "$2" ]; then
            NO_COLOR="$2" python3 -c 'import os, pty, sys; sys.exit(os.waitstatus_to_exitcode(pty.spawn([sys.argv[1]])))' "$probe" </dev/null >"$1" 2>/dev/null
        else
            env -u NO_COLOR python3 -c 'import os, pty, sys; sys.exit(os.waitstatus_to_exitcode(pty.spawn([sys.argv[1]])))' "$probe" </dev/null >"$1" 2>/dev/null
        fi
    else
        return 99
    fi
}

run_on_terminal "$work/tty.txt" ""
code=$?
if [ "$code" -eq 99 ]; then
    echo "SKIPPED (no pty tool): colour on a terminal and NO_COLOR on a terminal"
else
    if has_escape "$work/tty.txt"; then
        report ok "on a terminal the lines are coloured"
    else
        report bad "on a terminal the lines are coloured"
    fi
    run_on_terminal "$work/tty-nc.txt" 1
    if [ -s "$work/tty-nc.txt" ] && ! has_escape "$work/tty-nc.txt"; then
        report ok "on a terminal NO_COLOR=1 gives plain text"
    else
        report bad "on a terminal NO_COLOR=1 gives plain text"
    fi
fi

exit "$status"
