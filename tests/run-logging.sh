#!/bin/sh
# Logging checks that run outside the unit tests.
#
# Stanza 1: the library writes to the console only inside the default destination. The region between
# the lines "// console-output: begin" and "// console-output: end" (in any file of src/) is the default
# destination and is ignored, as are include/libecs-cpp/json.hpp and src/example.cpp. Any other
# std::cout, std::cerr, printf, puts, fputs or write to stdout or stderr in src/ or include/libecs-cpp/
# fails the stanza and each offending line is printed.

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

exit "$status"
