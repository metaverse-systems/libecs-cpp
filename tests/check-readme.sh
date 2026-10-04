#!/bin/sh
# Checks that the readme is accurate against the tree: there is exactly one readme, it carries no obsolete
# setup steps, every repository path it names exists, every configure option it names is defined in
# configure.ac (and every option configure.ac defines is documented), the packages it tells the reader to
# install are the ones the workflows and the cross-build record use, it has the sections a newcomer needs
# (requirements per task, configure options, running the tests, an orientation with a minimal program), and
# the package metadata has a real test command. A negative self-test plants a missing path and an unknown
# option in a copy of the readme and requires both to be reported.
#
# Assertions about the .github directory are skipped when the source tree has none, which is the case in a
# source archive.
#
# Environment: ECS_SRCDIR (in-tree fallback: the directory above this script).
#
# Exit status: 0 pass, 1 fail, 2 usage or tool problem.

here="$(cd "$(dirname "$0")" && pwd)"
srcdir="${ECS_SRCDIR:-$here/..}"
readme="$srcdir/README.md"
configure_ac="$srcdir/configure.ac"
package_json="$srcdir/package.json"

# Packages the readme may tell the reader to install: the set the hosted workflows install and the set the
# Windows cross build used.
allowed_packages="build-essential autoconf automake libtool pkg-config catch2 doxygen graphviz
g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev mingw-w64-tools
libz-mingw-w64-dev wine wine64 wine32"

# Text of the old setup block that must not come back.
obsolete_markers="sudo su -
dpkg --add-architecture i386
wixl
osslsigncode
mingw.thread.h
mingw.invoke.h
mingw.mutex.h
mingw-std-threads"

status=0

fail() {
    echo "FAIL: $*"
    status=1
}

for f in "$readme" "$configure_ac"; do
    if [ ! -f "$f" ]; then
        echo "FAIL: $f does not exist"
        exit 2
    fi
done

tmp="$(mktemp -d)" || { echo "FAIL: cannot create a temporary directory"; exit 2; }
trap 'rm -rf "$tmp"' EXIT INT TERM

# Prints the paths named in backticks that look like repository paths (one per line).
named_paths() {
    grep -oE '`+[^` ]+`+' "$1" | tr -d '`' | sed 's/[.,;:]*$//' |
        grep -E '^(tests/|src/|include/|\.github/)' | grep -v '[*?<>{}]' | grep -v '\.log$' | sort -u
}

# Prints the --enable-* and --disable-* options named on lines that do not say "retired".
named_options() {
    grep -vi 'retired' "$1" | grep -oE -- '--(enable|disable)-[A-Za-z0-9-]+' | sort -u
}

# Prints the option names configure.ac defines.
defined_options() {
    grep -oE 'AC_ARG_ENABLE\(\[[A-Za-z0-9-]+\]' "$configure_ac" | sed 's/.*\[//; s/\]//' | sort -u
}

# check_paths FILE: reports each named path that does not exist.
check_paths() {
    bad=0
    for p in $(named_paths "$1"); do
        case "$p" in
            .github/*) [ -d "$srcdir/.github" ] || continue ;;
        esac
        if [ ! -e "$srcdir/$p" ]; then
            echo "FAIL: the readme names $p, which does not exist"
            bad=1
        fi
    done
    return $bad
}

# check_options FILE: reports each named option that configure.ac does not define.
check_options() {
    bad=0
    defined="$(defined_options)"
    for o in $(named_options "$1"); do
        name="$(echo "$o" | sed -E 's/^--(enable|disable)-//')"
        if ! echo "$defined" | grep -qx -- "$name"; then
            echo "FAIL: the readme names $o, which configure.ac does not define"
            bad=1
        fi
    done
    return $bad
}

# section FILE REGEX: prints the part of the readme under the first heading that matches REGEX, up to the next
# heading of the same or a higher level (headings inside code blocks are ignored).
section() {
    awk -v re="$2" '
        /^```/ { fence = !fence }
        !fence && /^#+ / {
            match($0, /^#+/); level = RLENGTH
            if (on && level <= start) exit
            if (!on && tolower($0) ~ re) { on = 1; start = level; next }
        }
        on { print }
    ' "$1"
}

# --- exactly one readme ----------------------------------------------------------------------------------
others="$(find "$srcdir" -maxdepth 1 -iname 'README*' ! -name README.md | sed "s|^$srcdir/||" | sort)"
if [ -n "$others" ]; then
    fail "there is more than one readme: $(echo $others)"
else
    echo "PASS: README.md is the only readme"
fi

# --- obsolete setup steps --------------------------------------------------------------------------------
found=""
while IFS= read -r marker; do
    if grep -qF -- "$marker" "$readme"; then
        found="$found '$marker'"
    fi
done <<EOT
$obsolete_markers
EOT
if [ -n "$found" ]; then
    fail "the readme still has obsolete setup text:$found"
else
    echo "PASS: the readme has no obsolete setup text"
fi

# --- named paths and options -----------------------------------------------------------------------------
if [ -d "$srcdir/.github" ]; then
    :
else
    echo "SKIP: no .github directory in the source tree; paths under .github are not checked"
fi
out="$(check_paths "$readme")" && echo "PASS: every path the readme names exists" || { echo "$out"; status=1; }
out="$(check_options "$readme")" && echo "PASS: every option the readme names is defined in configure.ac" || { echo "$out"; status=1; }

missing=""
for name in $(defined_options); do
    grep -qE -- "--(enable|disable)-$name" "$readme" || missing="$missing --enable-$name"
done
if [ -n "$missing" ]; then
    fail "configure.ac defines options the readme does not document:$missing"
else
    echo "PASS: every option configure.ac defines is documented"
fi

# --- packages --------------------------------------------------------------------------------------------
packages="$(sed 's/`//g' "$readme" | awk '
    { line = $0 }
    joined { line = acc " " line; joined = 0 }
    line ~ /\\[ \t]*$/ { sub(/\\[ \t]*$/, "", line); acc = line; joined = 1; next }
    line ~ /apt(-get)? +(-[a-z-]+ +)*install/ {
        sub(/.*install[ \t]+/, "", line)
        sub(/[ \t]*(;|&&|\|).*/, "", line)
        n = split(line, w, /[ \t]+/)
        for (i = 1; i <= n; i++) {
            gsub(/[.,)]+$/, "", w[i])
            if (w[i] != "" && w[i] !~ /^-/) print w[i]
        }
    }
' | sort -u)"
unknown=""
for p in $packages; do
    case " $(echo $allowed_packages) " in
        *" $p "*) ;;
        *) unknown="$unknown $p" ;;
    esac
done
if [ -n "$unknown" ]; then
    fail "the readme tells the reader to install packages that no workflow or cross-build record uses:$unknown"
else
    echo "PASS: every package the readme names is a known one"
fi

# --- sections --------------------------------------------------------------------------------------------
req="$(section "$readme" 'requirements|prerequisites')"
if [ -z "$req" ]; then
    fail "the readme has no requirements section"
else
    miss=""
    for word in autoconf Catch2 doxygen mingw wine; do
        echo "$req" | grep -qi -- "$word" || miss="$miss $word"
    done
    if [ -n "$miss" ]; then
        fail "the requirements section does not cover (install, test, documentation, Windows):$miss"
    else
        echo "PASS: the requirements are stated per task"
    fi
fi

opts="$(section "$readme" 'configure options|build options')"
if [ -z "$opts" ]; then
    fail "the readme has no section on the configure options"
else
    miss=""
    for o in --prefix --host --enable-sanitizer --enable-werror --enable-tests; do
        echo "$opts" | grep -qF -- "$o" || miss="$miss $o"
    done
    if [ -n "$miss" ]; then
        fail "the configure options section does not cover:$miss"
    else
        echo "PASS: the configure options section covers the build options"
    fi
fi

tests="$(section "$readme" 'running the tests|^#+ tests')"
if [ -z "$tests" ]; then
    fail "the readme has no section on running the tests"
elif ! echo "$tests" | grep -qF 'make check'; then
    fail "the tests section does not show make check"
else
    echo "PASS: the readme shows how to run the tests"
fi

orient="$(section "$readme" 'orientation|overview|using the library')"
if [ -z "$orient" ]; then
    fail "the readme has no orientation section"
elif ! echo "$orient" | grep -qE '^```c(pp|\+\+)' || ! echo "$orient" | grep -q 'int main'; then
    fail "the orientation section has no minimal program"
else
    echo "PASS: the readme has an orientation with a minimal program"
fi

# --- package metadata ------------------------------------------------------------------------------------
if [ ! -f "$package_json" ]; then
    fail "package.json does not exist"
else
    script="$(sed -n 's/^[ \t]*"test"[ \t]*:[ \t]*"\(.*\)",\{0,1\}[ \t]*$/\1/p' "$package_json")"
    if [ -z "$script" ]; then
        fail "package.json has no test script"
    elif echo "$script" | grep -q 'no test specified'; then
        fail "the package.json test script is the placeholder: $script"
    elif ! echo "$script" | grep -qF 'make check'; then
        fail "the package.json test script does not run make check: $script"
    else
        echo "PASS: package.json has a real test script"
    fi
    if grep -qF 'npm test' "$readme" && [ -z "$script" ]; then
        fail "the readme documents npm test but package.json has no test script"
    fi
fi

# --- version agreement -----------------------------------------------------------------------------------
# The version in configure.ac, the version in package.json and the newest release-notes heading are one
# number, the readme states the shared-object name of this release (soname .so.2) and the rule for changing
# it, and no sentence still says the shared-object name is the old .so.0.
ac_version="$(sed -n 's/^AC_INIT(\[[^]]*\],\[\([^]]*\)\].*/\1/p' "$configure_ac" | head -n 1)"
pkg_version=""
[ -f "$package_json" ] && pkg_version="$(sed -n 's/^[ \t]*"version"[ \t]*:[ \t]*"\([^"]*\)".*/\1/p' "$package_json" | head -n 1)"
notes_version="$(section "$readme" '^## release notes' | sed -n 's/^### \([0-9][0-9.]*\).*/\1/p' | head -n 1)"
if [ -z "$ac_version" ] || [ -z "$pkg_version" ] || [ -z "$notes_version" ]; then
    fail "cannot read all three versions (configure.ac: '$ac_version', package.json: '$pkg_version', newest release notes: '$notes_version')"
elif [ "$ac_version" != "$pkg_version" ] || [ "$ac_version" != "$notes_version" ]; then
    fail "the versions disagree (configure.ac: $ac_version, package.json: $pkg_version, newest release notes: $notes_version)"
else
    echo "PASS: configure.ac, package.json and the newest release notes agree on $ac_version"
fi

stale="$(grep -nE 'so\.0' "$readme" | grep -iE 'stays|unchanged|soname|shared-object name|shared object name|name is' || true)"
if [ -n "$stale" ]; then
    fail "the readme still gives libecs-cpp.so.0 as the shared-object name: $(echo "$stale" | cut -c1-80 | tr '\n' ' ')"
elif ! grep -qE 'libecs-cpp\.so\.2([^.0-9]|$)' "$readme"; then
    fail "the readme does not state the libecs-cpp.so.2 soname"
else
    echo "PASS: the readme states the .so.2 soname and no longer gives .so.0 as the name"
fi

newest_notes="$(section "$readme" '^## release notes' | awk '/^### /{n++} n==1')"
if echo "$newest_notes" | grep -qE 'libecs-cpp\.so\.2([^.0-9]|$)'; then
    echo "PASS: the newest release notes name the .so.2 shared library"
else
    fail "the newest release notes do not name the libecs-cpp.so.2 shared library"
fi

if ! grep -qiE '^#+ .*versioning' "$readme"; then
    fail "the readme has no versioning-rule section"
else
    echo "PASS: the readme has a versioning-rule section"
fi
if ! grep -qiE '^(#+ .*|(\*\*)?)migration' "$readme"; then
    fail "the readme has no migration section"
else
    echo "PASS: the readme has a migration section"
fi

# --- negative self-test ----------------------------------------------------------------------------------
cp "$readme" "$tmp/README.md"
chmod u+w "$tmp/README.md"
{
    echo
    echo 'See `tests/no-such-file.sh` and configure with `--enable-no-such-option`.'
} >>"$tmp/README.md"
st=0
out="$(check_paths "$tmp/README.md")" && { fail "self-test: a reference to a missing file was not reported"; st=1; } ||
    echo "$out" | grep -qF 'tests/no-such-file.sh' || { fail "self-test: the missing file was not named"; st=1; }
out="$(check_options "$tmp/README.md")" && { fail "self-test: an unknown option was not reported"; st=1; } ||
    echo "$out" | grep -qF -- '--enable-no-such-option' || { fail "self-test: the unknown option was not named"; st=1; }
[ "$st" -eq 0 ] && echo "PASS: self-test: a missing file and an unknown option are reported"

exit "$status"
