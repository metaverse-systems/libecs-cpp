#!/bin/sh
# Runs the shipped example for a second and checks that it ends by itself with a clean shutdown:
# exit status 0, a start-up line for each system before its shutdown line, and no output after the
# last shutdown line. It also checks that the entity without a velocity is reported as skipped and that
# the output holds no runtime failure report. A start-up line ends with " started" and a shutdown line
# with " shut down".

example="$(dirname "$0")/../src/example"
output="$(mktemp)"
trap 'rm -f "$output"' EXIT INT TERM

timeout 30 "$example" 1 >"$output" 2>&1
status=$?
if [ "$status" -ne 0 ]; then
    echo "FAIL: example exited with status $status (124 means it did not end in time)"
    tail -n 20 "$output"
    exit 1
fi

last_start=$(grep -n ' started$' "$output" | tail -n 1 | cut -d: -f1)
first_shutdown=$(grep -n ' shut down$' "$output" | head -n 1 | cut -d: -f1)
last_shutdown=$(grep -n ' shut down$' "$output" | tail -n 1 | cut -d: -f1)
total=$(wc -l <"$output")

if [ -z "$last_start" ]; then
    echo "FAIL: no start-up line in the output"
    exit 1
fi
if [ -z "$first_shutdown" ]; then
    echo "FAIL: no shutdown line in the output"
    exit 1
fi
if [ "$last_start" -ge "$first_shutdown" ]; then
    echo "FAIL: a start-up line follows a shutdown line"
    exit 1
fi
if [ "$last_shutdown" -ne "$total" ]; then
    echo "FAIL: output follows the last shutdown line"
    tail -n 5 "$output"
    exit 1
fi

skipped=$(grep -c ' - has no velocity, skipped$' "$output")
if [ "$skipped" -lt 1 ]; then
    echo "FAIL: the entity without a velocity was not reported as skipped"
    tail -n 20 "$output"
    exit 1
fi

if grep -qiE 'runtime error|AddressSanitizer|Segmentation' "$output"; then
    echo "FAIL: the output reports a runtime failure"
    grep -iE 'runtime error|AddressSanitizer|Segmentation' "$output" | head -n 5
    exit 1
fi

echo "PASS: example ended cleanly"
exit 0
