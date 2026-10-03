#!/bin/sh
# Builds two small consumers from a staged installation using only what pkg-config prints for the installed
# library (plus the sanitizer flags of the build under test, which the metadata rightly does not carry), and
# runs them. One consumer includes the umbrella header, builds a world, adds a system and runs one update
# pass; the other includes a single header and builds an identifier through it. The metadata is also checked:
# its version, that it asks for C++20, that it satisfies the version the consumers ask for, that it keeps every path under the
# installation prefix (so the installation can be moved), and that its include directory holds the headers.
# When cross compiling the consumers are compiled and linked but not run. On a native Linux build a link
# against the static archive is tried as well.
#
# Exit status: 0 pass, 1 fail, 2 usage or tool problem.

here="$(cd "$(dirname "$0")" && pwd)"
builddir="${ECS_BUILDDIR:-$here/..}"
prefix="${ECS_PREFIX:-/usr/local}"
make_cmd="${ECS_MAKE:-make}"
cxx="${ECS_CXX:-c++}"
sanitizer_flags="${ECS_SANITIZER_FLAGS:-}"
cross="${ECS_CROSS:-no}"
host_os="${ECS_HOST_OS:-$(uname -s | tr 'A-Z' 'a-z')}"

if [ -z "$ECS_VERSION" ] && [ -f "$builddir/configure.ac" ]; then
    ECS_VERSION=$(sed -n 's/^AC_INIT(\[[^]]*\], *\[\([^]]*\)\].*/\1/p' "$builddir/configure.ac" | head -n 1)
fi
if [ -z "$ECS_VERSION" ]; then
    echo "FAIL: the library version is not known (ECS_VERSION is not set)"
    exit 2
fi

command -v pkg-config >/dev/null 2>&1 || { echo "FAIL: pkg-config is not installed"; exit 2; }

tmp="$(mktemp -d)" || { echo "FAIL: cannot create a temporary directory"; exit 2; }
trap 'rm -rf "$tmp"' EXIT INT TERM
stage="$tmp/stage"
sprefix="$stage$prefix"
mkdir -p "$stage" "$tmp/work" || exit 2

if ! $make_cmd -C "$builddir" install DESTDIR="$stage" >"$tmp/make.log" 2>&1; then
    echo "FAIL: make install DESTDIR failed"
    tail -n 20 "$tmp/make.log"
    exit 1
fi

# Only the staged metadata may be found, never one that happens to be installed on this machine.
PKG_CONFIG_PATH="$sprefix/lib/pkgconfig"
PKG_CONFIG_LIBDIR="$sprefix/lib/pkgconfig"
export PKG_CONFIG_PATH PKG_CONFIG_LIBDIR

status=0
fail() {
    echo "FAIL: $1"
    status=1
}

pc() {
    pkg-config --define-variable=prefix="$sprefix" "$@"
}

version=$(pc --modversion ecs-cpp 2>"$tmp/err")
if [ "$version" != "$ECS_VERSION" ]; then
    fail "pkg-config reports version '$version' instead of $ECS_VERSION $(cat "$tmp/err")"
fi
if ! pc --atleast-version=2.0.0 ecs-cpp; then
    fail "pkg-config says ecs-cpp is older than 2.0.0, the version the consumers ask for"
fi

if ! cflags=$(pc --cflags ecs-cpp 2>"$tmp/err") || ! libs=$(pc --libs ecs-cpp 2>>"$tmp/err"); then
    fail "pkg-config cannot print the flags of ecs-cpp: $(cat "$tmp/err")"
    exit 1
fi
echo "flags: $cflags $libs"

# Relocation: every path in the flags lies inside the stage.
for word in $cflags $libs; do
    case "$word" in
        -I*) dir="${word#-I}" ;;
        -L*) dir="${word#-L}" ;;
        *) continue ;;
    esac
    case "$dir" in
        "$stage"/*) ;;
        *) fail "the metadata names a path outside the installation: $word" ;;
    esac
done

# The headers need C++20. A compiler may default to it, which would hide a missing flag, so the metadata
# itself must ask for it.
std_ok=no
for word in $cflags; do
    case "$word" in
        -std=c++20|-std=gnu++20|-std=c++2[3-9]|-std=gnu++2[3-9]|-std=c++2b|-std=gnu++2b) std_ok=yes ;;
    esac
done
if [ "$std_ok" != "yes" ]; then
    fail "the metadata does not ask for C++20 (-std=c++20): $cflags"
fi

include_dir=""
for word in $cflags; do
    case "$word" in
        -I*) [ -f "${word#-I}/libecs-cpp/ecs.hpp" ] && include_dir="${word#-I}" ;;
    esac
done
if [ -z "$include_dir" ]; then
    fail "no include directory printed by pkg-config holds libecs-cpp/ecs.hpp"
fi

cat >"$tmp/work/world.cpp" <<'SRC'
#include <libecs-cpp/ecs.hpp>
#include <chrono>
#include <memory>
#include <thread>

namespace
{
    int passes = 0;

    class CountingSystem : public ecs::System
    {
      public:
        CountingSystem() : ecs::System("CountingSystem") {}

        nlohmann::json Export() const
        {
            nlohmann::json config;
            config["Handle"] = this->Handle;
            return config;
        }

        void Update() { ++passes; }
    };
}

int main()
{
    auto world = ECS->Container();
    world->System(std::make_unique<CountingSystem>());
    world->SystemsInitialize();
    // A system's first update is due one interval after it starts, so pass until it has run, at most about a second.
    for(int attempt = 0; attempt < 100 && passes == 0; attempt++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        world->Update();
    }
    ECS->Shutdown();
    return passes >= 1 ? 0 : 1;
}
SRC

cat >"$tmp/work/single.cpp" <<'SRC'
#include <libecs-cpp/Container.hpp>
#include <string>

int main()
{
    ecs::Uuid id;
    ecs::Uuid same(id.Get());
    return (id.Get().size() == 36 && same.Get() == id.Get()) ? 0 : 1;
}
SRC

exe=""
[ "$cross" = "yes" ] && exe=".exe"

# Compile and link with nothing but the metadata and the sanitizer flags; the source goes first so the
# libraries that follow it are used.
build() {
    # usage: build <name> <extra link flags>
    name="$1"; shift
    src="$tmp/work/${name%%-*}.cpp"
    # shellcheck disable=SC2086
    if ! $cxx "$src" -o "$tmp/work/$name$exe" $cflags $libs "$@" $sanitizer_flags >"$tmp/work/$name.log" 2>&1; then
        fail "$name does not build from the pkg-config flags"
        sed 's/^/    /' "$tmp/work/$name.log" | head -n 15
        return 1
    fi
    return 0
}

run() {
    # usage: run <name>
    if [ "$cross" = "yes" ]; then
        echo "SKIP: $1 is not run when cross compiling"
        return 0
    fi
    if ! LD_LIBRARY_PATH="$sprefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$tmp/work/$1" >"$tmp/work/$1.out" 2>&1; then
        fail "$1 built but does not run correctly"
        sed 's/^/    /' "$tmp/work/$1.out" | head -n 15
    fi
}

for name in world single; do
    if build "$name"; then
        run "$name"
    fi
done

if [ "$cross" != "yes" ] && [ "$host_os" != "${host_os#linux}" ]; then
    if [ -f "$sprefix/lib/libecs-cpp.a" ]; then
        if build world-static -Wl,-Bstatic -lecs-cpp -Wl,-Bdynamic -pthread; then
            run world-static
        fi
    else
        fail "the installation has no static archive to link against"
    fi
else
    echo "SKIP: the static link is tried only for native Linux builds"
fi

if [ "$status" -eq 0 ]; then
    echo "PASS: consumers build from the pkg-config flags of ecs-cpp $version"
fi
exit "$status"
