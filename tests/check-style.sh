#!/bin/sh
# Checks the project's naming rules for members in the library headers and sources.
#
# Usage: check-style.sh [LIBRARY_ROOT]
#   LIBRARY_ROOT  the libecs-cpp source directory (default: the directory above this script)
#
# Files read: include/libecs-cpp/*.hpp (except json.hpp), src/*.cpp and src/*.hpp.
# Rules, applied to code with // comments removed:
#   1. A name with a trailing underscore (for example "count_") fails: member names carry no trailing
#      underscore.
#   2. In a header, a data member declared after "private:" whose name is snake_case (lower case letters
#      and digits joined with an underscore, for example "system_order") fails: private members are camelCase.
# The "this->" rule is not checked here, because a pattern cannot tell a member from a local variable
# reliably; it is checked by review.
# Each offending line is printed as FILE:LINE: text, and the exit status is 1 when any rule fails. The check
# is Linux-only and exits with status 77 (skipped) on any other system. A usage problem exits with status 2.

if [ "$(uname -s)" != "Linux" ]; then
    echo "check-style: skipped, this check runs on Linux only"
    exit 77
fi

if [ $# -gt 1 ]; then
    echo "usage: $0 [LIBRARY_ROOT]" >&2
    exit 2
fi
root="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
if [ ! -d "$root/include/libecs-cpp" ] || [ ! -d "$root/src" ]; then
    echo "check-style: no include/libecs-cpp or src in $root" >&2
    exit 2
fi

files=""
for file in "$root"/include/libecs-cpp/*.hpp "$root"/src/*.cpp "$root"/src/*.hpp; do
    case "$file" in
        */json.hpp) continue ;;
    esac
    [ -f "$file" ] && files="$files $file"
done

# shellcheck disable=SC2086
awk '
    FNR == 1 { section = "public"; block = 0; depth = 0; pending = "" }
    {
        line = $0
        # Remove block comments that start and end on one line, then line comments and the rest of a block.
        gsub(/\/\*[^*]*\*\//, "", line)
        if (block) {
            if (index(line, "*/") == 0) next
            sub(/^.*\*\//, "", line)
            block = 0
        }
        if (index(line, "/*") > 0) { sub(/\/\*.*$/, "", line); block = 1 }
        sub(/\/\/.*$/, "", line)
        # Remove string and character literals so that their text is not read as code.
        gsub(/"([^"\\]|\\.)*"/, "\"\"", line)
        gsub(/\047([^\047\\]|\\.)*\047/, "\047\047", line)

        # Rule 1: a trailing underscore on any name.
        if (line ~ /(^|[^A-Za-z0-9_])[A-Za-z][A-Za-z0-9_]*[A-Za-z0-9]_([^A-Za-z0-9_]|$)/) {
            printf "%s:%d: trailing underscore: %s\n", FILENAME, FNR, $0
            bad = 1
        }

        # Rule 2: snake_case data member after private: in a header.
        if (FILENAME !~ /\.hpp$/) next
        if (line ~ /^[ \t]*(public|protected|private)[ \t]*:/) {
            section = line
            sub(/^[ \t]*/, "", section)
            sub(/[ \t]*:.*$/, "", section)
            next
        }
        # A class or struct head (not a declaration or a use) opens a scope at its next "{": a class starts
        # private, a struct public. The previous scope is restored at the closing "}".
        if (line ~ /^[ \t]*(class|struct)[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*(:[^;]*)?(\{.*)?$/ && line !~ /;/)
            pending = (line ~ /^[ \t]*class/) ? "private" : "public"
        opens = gsub(/\{/, "{", line)
        closes = gsub(/\}/, "}", line)
        for (i = 0; i < opens; i++) {
            depth++
            savedSection[depth] = section
            if (pending != "") { kind[depth] = "class"; section = pending; pending = "" }
            else kind[depth] = "other"
        }
        for (i = 0; i < closes; i++) {
            section = savedSection[depth]
            depth--
        }
        if (depth == 0 || kind[depth] != "class") next
        if (section == "private" && line ~ /;[ \t]*$/ && line !~ /\(/ && line !~ /^[ \t]*(using|typedef|friend|return)/) {
            decl = line
            sub(/[ \t]*(=|\{).*$/, "", decl)
            sub(/;[ \t]*$/, "", decl)
            n = split(decl, parts, /[^A-Za-z0-9_]+/)
            name = parts[n]
            if (name ~ /^[a-z][a-z0-9]*(_[a-z0-9]+)+$/) {
                printf "%s:%d: snake_case private member: %s\n", FILENAME, FNR, $0
                bad = 1
            }
        }
    }
    END { exit bad }
' $files
status=$?
if [ "$status" -eq 0 ]; then
    echo "PASS: no trailing-underscore or snake_case private members"
fi
exit "$status"
