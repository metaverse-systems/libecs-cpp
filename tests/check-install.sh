#!/bin/sh
# Stages an installation with DESTDIR and checks what it puts on the machine: exactly the library, its
# headers and its build metadata, and no program. It then removes the installation again and checks that
# nothing is left, and checks that installing over a stage removes the headers that earlier releases
# installed and this one retired. Nothing is run from the staged tree, so it also works for cross builds.
#
# Exit status: 0 pass, 1 fail, 2 usage or tool problem.

here="$(cd "$(dirname "$0")" && pwd)"
builddir="${ECS_BUILDDIR:-$here/..}"
prefix="${ECS_PREFIX:-/usr/local}"
make_cmd="${ECS_MAKE:-make}"

host_os="$ECS_HOST_OS"
if [ -z "$host_os" ] && [ -f "$builddir/libtool" ]; then
    host_os=$(sed -n 's/^host_os=//p' "$builddir/libtool" | head -n 1)
fi
if [ -z "$host_os" ]; then
    host_os=$(uname -s | tr 'A-Z' 'a-z')
fi

# The files an installation holds, relative to the prefix, one per line, for each kind of host.
# This is the install layout the consumers rely on.
headers="include/libecs-cpp/Clock.hpp
include/libecs-cpp/Component.hpp
include/libecs-cpp/Container.hpp
include/libecs-cpp/Entity.hpp
include/libecs-cpp/Manager.hpp
include/libecs-cpp/Resource.hpp
include/libecs-cpp/System.hpp
include/libecs-cpp/Timing.hpp
include/libecs-cpp/Uuid.hpp
include/libecs-cpp/ecs.hpp
include/libecs-cpp/json.hpp
lib/pkgconfig/ecs-cpp.pc"
linux_expected="$headers
lib/libecs-cpp.a
lib/libecs-cpp.la
lib/libecs-cpp.so
lib/libecs-cpp.so.0
lib/libecs-cpp.so.0.0.0"
mingw_expected="$headers
lib/libecs-cpp.a
lib/libecs-cpp.dll.a
lib/libecs-cpp.la
bin/libecs-cpp-0.dll"

case "$host_os" in
    mingw*|cygwin*|msys*) expected="$mingw_expected"; windows=yes ;;
    *) expected="$linux_expected"; windows=no ;;
esac

tmp="$(mktemp -d)" || { echo "FAIL: cannot create a temporary directory"; exit 2; }
trap 'rm -rf "$tmp"' EXIT INT TERM
stage="$tmp/stage"
root="$stage$prefix"
status=0

run_make() {
    $make_cmd -C "$builddir" "$@" DESTDIR="$stage" >"$tmp/make.log" 2>&1
}

list_files() {
    if [ -d "$root" ]; then
        (cd "$root" && find . ! -type d | sed 's|^\./||' | LC_ALL=C sort)
    fi
}

mkdir -p "$stage" || exit 2
if ! run_make install; then
    echo "FAIL: make install DESTDIR failed"
    tail -n 20 "$tmp/make.log"
    exit 1
fi

list_files >"$tmp/actual"
echo "$expected" | LC_ALL=C sort >"$tmp/expected"

missing=$(LC_ALL=C comm -13 "$tmp/actual" "$tmp/expected")
extra=$(LC_ALL=C comm -23 "$tmp/actual" "$tmp/expected")
if [ -n "$missing" ]; then
    echo "$missing" | while IFS= read -r f; do echo "FAIL: missing from the install: $f"; done
    status=1
fi
if [ -n "$extra" ]; then
    echo "$extra" | while IFS= read -r f; do echo "FAIL: unexpected in the install: $f"; done
    status=1
fi

# No program may be installed; the only executable under bin is the Windows library.
programs=$(cd "$stage" 2>/dev/null && find . -type f -perm -u+x \( -path '*/bin/*' -o -path '*/sbin/*' \) | sed 's|^\./||' | LC_ALL=C sort)
if [ -n "$programs" ]; then
    echo "$programs" | while IFS= read -r f; do
        case "$windows:$f" in
            yes:*/bin/libecs-cpp-*.dll) ;;
            *) echo "FAIL: program installed: ${f#"${prefix#/}"/}" ;;
        esac
    done | tee "$tmp/programs"
    if [ -s "$tmp/programs" ]; then
        status=1
    fi
fi

if ! run_make uninstall; then
    echo "FAIL: make uninstall DESTDIR failed"
    tail -n 20 "$tmp/make.log"
    status=1
fi
left=$(list_files)
if [ -n "$left" ]; then
    echo "$left" | while IFS= read -r f; do echo "FAIL: left after uninstall: $f"; done
    status=1
fi

# A retired header from an earlier release must be removed by installing over it.
mkdir -p "$root/include/libecs-cpp" || exit 2
: >"$root/include/libecs-cpp/uuid_v4.h"
if ! run_make install; then
    echo "FAIL: make install over an existing stage failed"
    tail -n 20 "$tmp/make.log"
    exit 1
fi
if [ -e "$root/include/libecs-cpp/uuid_v4.h" ]; then
    echo "FAIL: retired header left by install: include/libecs-cpp/uuid_v4.h"
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "PASS: install holds exactly the expected files, uninstall leaves none, retired headers are removed"
fi
exit "$status"
