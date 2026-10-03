#!/bin/sh
# Negative self-test for check-style.sh. It builds a small stand-in library tree and checks that the style
# check accepts a clean header and rejects a trailing-underscore member, a snake_case private member and a
# trailing-underscore name in a source file. It does not depend on the library.

here="$(cd "$(dirname "$0")" && pwd)"
check="$here/check-style.sh"

if [ "$(uname -s)" != "Linux" ]; then
    echo "check-style-selftest: skipped, this check runs on Linux only"
    exit 77
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT INT TERM
failures=0

# fresh HEADER_BODY SOURCE_BODY: lay out a stand-in tree under $work/tree.
fresh()
{
    rm -rf "$work/tree"
    mkdir -p "$work/tree/include/libecs-cpp" "$work/tree/src"
    printf '%s\n' "$1" >"$work/tree/include/libecs-cpp/Widget.hpp"
    printf '%s\n' "$2" >"$work/tree/src/Widget.cpp"
    # A name with an underscore in the excluded third-party header must not matter.
    printf 'int vendor_name_;\n' >"$work/tree/include/libecs-cpp/json.hpp"
}

# expect STATUS LABEL: run the check on the stand-in tree and compare its exit status.
expect()
{
    "$check" "$work/tree" >"$work/out.txt" 2>&1
    got=$?
    if [ "$got" -ne "$1" ]; then
        echo "FAIL: $2 (expected status $1, got $got)"
        cat "$work/out.txt"
        failures=$((failures + 1))
    fi
}

clean_header='class Widget
{
public:
    int publicValue;
    int Value() const;
private:
    int systemOrder;
    int count = 0; // not a snake_case_name
    void helper_function();
};'
clean_source='#include "Widget.hpp"
int Widget::Value() const { return this->count; }'

fresh "$clean_header" "$clean_source"
expect 0 "a clean tree passes"

fresh 'class Widget
{
private:
    int count_;
};' "$clean_source"
expect 1 "a trailing-underscore member fails"

fresh 'class Widget
{
private:
    int system_order;
};' "$clean_source"
expect 1 "a snake_case private member fails"

fresh 'namespace ecs
{
    class Widget
    {
    private:
        struct Slot
        {
            int started;
        };
        int body() { int local_value = 1; return local_value; }
        int system_order;
    };
}' "$clean_source"
expect 1 "a snake_case private member after a nested struct fails"

fresh "$clean_header" 'int Widget::Value() const { return this->count_; }'
expect 1 "a trailing underscore in a source file fails"

fresh 'class Widget
{
public:
    int some_public;
};' "$clean_source"
expect 0 "a snake_case public member is not this check's concern"

if [ "$failures" -eq 0 ]; then
    echo "PASS: the style check accepts clean code and rejects each violation"
fi
[ "$failures" -eq 0 ]
