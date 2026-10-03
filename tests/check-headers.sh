#!/bin/sh
# Compiles every installed public header on its own, the way a consumer would include it: the header alone,
# the header twice, the umbrella header followed by it, and it followed by the umbrella header. The headers
# are taken from a staged installation, so what is checked is what is installed. The compiler only checks the
# syntax, so nothing is linked or run and the check also works for cross builds.
#
# Exit status: 0 pass, 1 fail, 2 usage or tool problem.

here="$(cd "$(dirname "$0")" && pwd)"
builddir="${ECS_BUILDDIR:-$here/..}"
prefix="${ECS_PREFIX:-/usr/local}"
make_cmd="${ECS_MAKE:-make}"
cxx="${ECS_CXX:-c++}"
jobs="${ECS_JOBS:-4}"

tmp="$(mktemp -d)" || { echo "FAIL: cannot create a temporary directory"; exit 2; }
trap 'rm -rf "$tmp"' EXIT INT TERM
stage="$tmp/stage"
include="$stage$prefix/include"
mkdir -p "$stage" "$tmp/tu" || exit 2

if ! $make_cmd -C "$builddir" install DESTDIR="$stage" >"$tmp/make.log" 2>&1; then
    echo "FAIL: make install DESTDIR failed"
    tail -n 20 "$tmp/make.log"
    exit 1
fi

if [ ! -d "$include/libecs-cpp" ]; then
    echo "FAIL: the install has no include/libecs-cpp directory"
    exit 1
fi
(cd "$include/libecs-cpp" && find . -name '*.hpp' | sed 's|^\./||' | LC_ALL=C sort) >"$tmp/headers"
count=$(wc -l <"$tmp/headers" | tr -d ' ')
if [ "$count" -eq 0 ]; then
    echo "FAIL: the install holds no public header"
    exit 1
fi

# One translation unit per header and mode; the job list holds "file header mode" per line.
: >"$tmp/jobs"
n=0
while IFS= read -r header; do
    n=$((n + 1))
    for mode in alone twice umbrella-first umbrella-last; do
        tu="$tmp/tu/$n-$mode.cpp"
        case "$mode" in
            alone)         printf '#include <libecs-cpp/%s>\n' "$header" >"$tu" ;;
            twice)         printf '#include <libecs-cpp/%s>\n#include <libecs-cpp/%s>\n' "$header" "$header" >"$tu" ;;
            umbrella-first) printf '#include <libecs-cpp/ecs.hpp>\n#include <libecs-cpp/%s>\n' "$header" >"$tu" ;;
            umbrella-last)  printf '#include <libecs-cpp/%s>\n#include <libecs-cpp/ecs.hpp>\n' "$header" >"$tu" ;;
        esac
        printf '%s %s %s\n' "$tu" "$header" "$mode" >>"$tmp/jobs"
    done
done <"$tmp/headers"

cat >"$tmp/compile.sh" <<'WORKER'
#!/bin/sh
# usage: compile.sh <file> <header> <mode>; on failure leaves a FAIL line and the first compiler messages in <file>.fail
if ! $ECS_HEADERS_CXX -std=c++20 -Wall -Wextra -Werror -fsyntax-only -I"$ECS_HEADERS_INCLUDE" "$1" >"$1.log" 2>&1; then
    {
        echo "FAIL: $2 does not compile ($3)"
        grep -m 5 -E 'error|warning' "$1.log" | sed 's/^/    /'
    } >"$1.fail"
fi
exit 0
WORKER

ECS_HEADERS_CXX="$cxx"
ECS_HEADERS_INCLUDE="$include"
export ECS_HEADERS_CXX ECS_HEADERS_INCLUDE
xargs -L 1 -P "$jobs" sh "$tmp/compile.sh" <"$tmp/jobs" || { echo "FAIL: could not run the compiler jobs"; exit 2; }

status=0
for f in "$tmp"/tu/*.fail; do
    [ -f "$f" ] || continue
    cat "$f"
    status=1
done
if [ "$status" -eq 0 ]; then
    echo "PASS: $count public headers compile alone, twice and with the umbrella header"
fi
exit "$status"
