#!/bin/sh
# Checks that a built libecs-cpp shared library exports the public names and no others.
#
# Usage: check-exports.sh LIBRARY INCLUDE_DIR
#        check-exports.sh            (reads ECS_EXPORT_LIBRARY and ECS_EXPORT_INCLUDE; "make check" does this)
#   LIBRARY      the shared library to inspect (for a build tree, src/.libs/libecs-cpp.so)
#   INCLUDE_DIR  the directory that holds libecs-cpp/*.hpp (or the directory with the headers itself)
#
# Rules, applied to the output of "nm -D --defined-only -C":
#   1. A defined symbol in the global namespace fails unless it is declared "extern" in ecs.hpp (ECS) or
#      is a toolchain symbol (_init, _fini, _edata, _end, __bss_start, or any name starting with "_").
#   2. A defined symbol in namespace ecs fails unless the first name after "ecs::" is declared in a public
#      header (class, struct, enum, using, typedef, a constant or a function at namespace scope) or is
#      listed in INTERNAL_NAMES below.
#   3. A class or function defined in a public header fails when the library holds no symbol of ecs::NAME,
#      except for the types in HEADER_ONLY below, which have no code outside the header.
# Symbols of std::, __gnu_cxx:: and nlohmann::, with their typeinfo, vtable and guard variable entries,
# are ignored. Each offending name is printed and the exit status is 1 when any rule fails. A usage or
# tool problem exits with status 2.

# Namespaces that come from internal headers in src/ and appear as weak symbols of inline functions.
INTERNAL_NAMES="validation"
# Types that hold data or inline code only, so the library has nothing to export for them. Whether a
# compiler emits weak copies of such inline code into the library depends on the compiler and on what the
# library itself uses, so these are never required to appear.
HEADER_ONLY="Resource Timing Mailbox Timer Component Clock SteadyClock ManualClock"

if [ $# -eq 0 ]; then
    set -- "${ECS_EXPORT_LIBRARY:-}" "${ECS_EXPORT_INCLUDE:-}"
fi
if [ $# -ne 2 ] || [ -z "$1" ] || [ -z "$2" ]; then
    echo "usage: $0 LIBRARY INCLUDE_DIR" >&2
    exit 2
fi
library="$1"
include="$2"
if [ ! -f "$library" ]; then
    echo "check-exports: no such library: $library" >&2
    exit 2
fi
if [ -d "$include/libecs-cpp" ]; then
    include="$include/libecs-cpp"
fi
if [ ! -f "$include/ecs.hpp" ]; then
    echo "check-exports: no ecs.hpp in $include" >&2
    exit 2
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT INT TERM

# Public names. "N name" lists every declared name, "D name" lists classes, structs and functions
# that a header defines.
for header in "$include"/*.hpp; do
    case "$header" in
        */json.hpp) continue ;;
    esac
    awk '
        /^extern / {
            line = $0
            sub(/;.*/, "", line)
            n = split(line, parts, /[^A-Za-z0-9_]+/)
            if (n > 0) print "G " parts[n]
            next
        }
        /^    (class|struct) [A-Za-z_][A-Za-z0-9_]*;/ {
            name = $2; sub(/;.*/, "", name); print "N " name; next
        }
        /^    (class|struct) [A-Za-z_]/ {
            name = $2; sub(/[:{ ].*/, "", name); print "N " name; print "D " name; next
        }
        /^    enum / {
            name = $2
            if (name == "class" || name == "struct") name = $3
            sub(/[:{; ].*/, "", name); print "N " name; next
        }
        /^    using [A-Za-z_][A-Za-z0-9_]* *=/ { name = $2; print "N " name; next }
        /^    typedef .*;/ {
            line = $0
            sub(/;.*/, "", line)
            n = split(line, parts, /[^A-Za-z0-9_]+/)
            print "N " parts[n]
            next
        }
        /^    (static_assert|namespace|template|return|using|if|for|while|else)/ { next }
        /^    (inline |static |constexpr |const )*[A-Za-z_][A-Za-z0-9_:<>,*& ]* [A-Za-z_][A-Za-z0-9_]* *(=|\{)/ {
            line = $0
            sub(/ *(=|\{).*/, "", line)
            n = split(line, parts, /[^A-Za-z0-9_]+/)
            print "N " parts[n]
            next
        }
        /^    (inline |static |constexpr |const )*[A-Za-z_][A-Za-z0-9_:<>,*& ]*[ *&][A-Za-z_][A-Za-z0-9_]*\(/ {
            line = $0
            sub(/\(.*/, "", line)
            n = split(line, parts, /[^A-Za-z0-9_]+/)
            print "N " parts[n]
            print "D " parts[n]
        }
    ' "$header"
done >"$work/public.txt"

nm -D --defined-only -C "$library" >"$work/nm.txt" 2>"$work/nm.err"
if [ $? -ne 0 ] || [ ! -s "$work/nm.txt" ]; then
    echo "check-exports: nm could not read $library" >&2
    cat "$work/nm.err" >&2
    exit 2
fi

# Classify every symbol: "G name" for the global namespace, "E name" for ecs::name.
sed -E 's/^[0-9a-fA-F]+ [A-Za-z] //' "$work/nm.txt" | awk '
    {
        s = $0
        while (match(s, /^(non-virtual thunk to |virtual thunk to |covariant return thunk to |typeinfo name for |typeinfo for |vtable for |VTT for |guard variable for |construction vtable for |TLS init function for |TLS wrapper function for |reference temporary #[0-9]+ for )/))
            s = substr(s, RLENGTH + 1)
        # A template function is listed with its return type in front; drop built-in type words.
        while (match(s, /^(void|bool|int|char|short|long|unsigned|signed|float|double|const|timespec) /))
            s = substr(s, RLENGTH + 1)
        # A return type written as decltype ((...)(0)) ends at the first "(0)) ".
        if (substr(s, 1, 9) == "decltype " && (i = index(s, ")) ")) > 0)
            s = substr(s, i + 3)
        if (!match(s, /^[A-Za-z_][A-Za-z0-9_]*/)) next
        first = substr(s, 1, RLENGTH)
        rest = substr(s, RLENGTH + 1)
        if (first == "std" || first == "__gnu_cxx" || first == "nlohmann") next
        if (first == "ecs" && substr(rest, 1, 2) == "::") {
            rest = substr(rest, 3)
            if (match(rest, /^[A-Za-z_][A-Za-z0-9_]*/)) print "E " substr(rest, 1, RLENGTH)
            next
        }
        print "G " first
    }
' | sort -u >"$work/symbols.txt"

status=0
fail()
{
    echo "FAIL: $1"
    status=1
}

# Rule 1.
allowed_global="$(awk '$1 == "G" { print $2 }' "$work/public.txt")"
for name in $(awk '$1 == "G" { print $2 }' "$work/symbols.txt"); do
    case "$name" in
        _*) continue ;;
    esac
    found=0
    for ok in $allowed_global; do
        [ "$name" = "$ok" ] && found=1
    done
    [ "$found" -eq 1 ] || fail "rule 1: global-namespace symbol not declared by the public interface: $name"
done

# Rule 2.
declared="$(awk '$1 == "N" { print $2 }' "$work/public.txt")"
for name in $(awk '$1 == "E" { print $2 }' "$work/symbols.txt"); do
    found=0
    for ok in $declared $INTERNAL_NAMES; do
        [ "$name" = "$ok" ] && found=1
    done
    [ "$found" -eq 1 ] || fail "rule 2: ecs::$name is exported but no public header declares it"
done

# Rule 3.
for name in $(awk '$1 == "D" { print $2 }' "$work/public.txt" | sort -u); do
    skip=0
    for ok in $HEADER_ONLY; do
        [ "$name" = "$ok" ] && skip=1
    done
    [ "$skip" -eq 1 ] && continue
    awk -v n="$name" '$1 == "E" && $2 == n { found = 1 } END { exit !found }' "$work/symbols.txt" ||
        fail "rule 3: ecs::$name is declared in a public header but the library exports nothing for it"
done

if [ "$status" -eq 0 ]; then
    echo "PASS: only public names are exported"
fi
exit "$status"
