#!/bin/sh
# Negative self-test for check-exports.sh. It builds three tiny shared objects against a small stand-in
# set of public headers and checks that the export check accepts the clean one and rejects the two that
# export a name outside the public interface (one in the global namespace, one in ecs::detail). It does
# not depend on the library.

here="$(cd "$(dirname "$0")" && pwd)"
check="$here/check-exports.sh"
compiler="${CXX:-c++}"
SHELL="${SHELL:-/bin/sh}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT INT TERM

mkdir -p "$work/include/libecs-cpp"
cat >"$work/include/libecs-cpp/ecs.hpp" <<'HEADER'
#pragma once
namespace ecs
{
    class Widget
    {
    public:
        int Value();
    };
}
extern ecs::Widget *ECS;
HEADER

cat >"$work/clean.cpp" <<'SOURCE'
#include <libecs-cpp/ecs.hpp>
ecs::Widget *ECS = nullptr;
int ecs::Widget::Value() { return 1; }
SOURCE

cat >"$work/global.cpp" <<'SOURCE'
#include <libecs-cpp/ecs.hpp>
ecs::Widget *ECS = nullptr;
int ecs::Widget::Value() { return 1; }
int leakedHelper() { return 2; }
SOURCE

cat >"$work/nested.cpp" <<'SOURCE'
#include <libecs-cpp/ecs.hpp>
ecs::Widget *ECS = nullptr;
int ecs::Widget::Value() { return 1; }
namespace ecs::detail
{
    int hiddenHelper();
    int hiddenHelper() { return 3; }
}
SOURCE

status=0
for name in clean global nested; do
    if ! "$compiler" -std=c++20 -shared -fPIC -I"$work/include" -o "$work/$name.so" "$work/$name.cpp"; then
        echo "FAIL: could not build the $name object"
        exit 1
    fi
done

if $SHELL "$check" "$work/clean.so" "$work/include" >"$work/clean.out" 2>&1; then
    echo "PASS: the clean object is accepted"
else
    echo "FAIL: the clean object was rejected"
    cat "$work/clean.out"
    status=1
fi

for pair in global:leakedHelper nested:detail; do
    name="${pair%%:*}"
    leak="${pair##*:}"
    if $SHELL "$check" "$work/$name.so" "$work/include" >"$work/$name.out" 2>&1; then
        echo "FAIL: the $name object was accepted"
        status=1
    elif grep -q "$leak" "$work/$name.out"; then
        echo "PASS: the $name object is rejected and $leak is named"
    else
        echo "FAIL: the $name object was rejected without naming $leak"
        cat "$work/$name.out"
        status=1
    fi
done

exit "$status"
