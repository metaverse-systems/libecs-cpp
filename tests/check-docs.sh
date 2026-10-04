#!/bin/sh
# Checks the documentation set-up: how Doxygen is configured, what the generated site contains, and that the
# workflows that build and publish it can only read the repository. The configuration is read from the
# Doxyfile that configure generated; Doxygen is then run into a temporary directory so nothing is written
# into the source or build tree. The workflow assertions are skipped when the source tree has no .github
# directory, which is the case in a source archive.
#
# Environment: ECS_SRCDIR, ECS_BUILDDIR (in-tree fallbacks), ECS_REQUIRE_DOCS=1 makes a missing doxygen a
# failure instead of a skip.
#
# Exit status: 0 pass, 1 fail, 2 usage or tool problem, 77 doxygen is missing and not required.

here="$(cd "$(dirname "$0")" && pwd)"
srcdir="${ECS_SRCDIR:-$here/..}"
builddir="${ECS_BUILDDIR:-$here/..}"
doxyfile="$builddir/Doxyfile"
status=0
nfail=0

fail() {
    echo "FAIL: $*"
    status=1
    nfail=$((nfail + 1))
}

tmp="$(mktemp -d)" || { echo "FAIL: cannot create a temporary directory"; exit 2; }
trap 'rm -rf "$tmp"' EXIT INT TERM

# --- generated documentation must not be tracked ---------------------------------------------------------
if [ -e "$srcdir/.git" ] && command -v git >/dev/null 2>&1 &&
   git -C "$srcdir" rev-parse --git-dir >/dev/null 2>&1; then
    tracked=$(git -C "$srcdir" ls-files -- docs | while IFS= read -r f; do
        [ -e "$srcdir/$f" ] && echo x
    done | wc -l | tr -d ' ')
    if [ "$tracked" -ne 0 ]; then
        fail "$tracked generated documentation files are tracked under docs/"
    else
        echo "PASS: no generated documentation is tracked"
    fi
else
    echo "SKIP: the source tree is not a git checkout; tracked files not checked"
fi

# --- Doxygen configuration -------------------------------------------------------------------------------
if [ ! -f "$doxyfile" ]; then
    echo "FAIL: $doxyfile does not exist (run configure first)"
    exit 2
fi

# Prints the value of a Doxyfile tag: continuation lines joined, "+=" appended, the last "=" wins.
dox_get() {
    awk -v tag="$1" '
        { line = $0 }
        cont { buf = buf " " line; cont = 0; if (sub(/\\[ \t]*$/, "", buf)) cont = 1; else flush(); next }
        function flush() { if (op == "=") val = buf; else val = val " " buf }
        $1 == tag && ($2 == "=" || $2 == "+=") {
            op = $2; sub(/^[^=]*=[ \t]*/, "", line); buf = line
            if (sub(/\\[ \t]*$/, "", buf)) cont = 1; else flush()
        }
        END { print val }
    ' "$doxyfile" | sed 's/^ *//; s/ *$//'
}

input="$(dox_get INPUT)"
if [ -z "$input" ]; then
    fail "the Doxygen configuration has no INPUT"
else
    bad=""
    for item in $input; do
        case "$item" in
            */include/libecs-cpp|*/include/libecs-cpp/|*/README.md|*/GUIDE.md|*/REFERENCE.md|*/CONTRIBUTING.md|*/NEWS.md) ;;
            *) bad="$bad $item" ;;
        esac
    done
    if [ -n "$bad" ]; then
        fail "Doxygen INPUT is not limited to the public headers and the user documents:$bad"
    else
        echo "PASS: Doxygen reads only the public headers and the user documents"
    fi
fi

recursive="$(dox_get RECURSIVE)"
if [ "$recursive" != "NO" ]; then
    fail "RECURSIVE is '$recursive', it must be NO so no subdirectory is read"
fi

patterns="$(dox_get FILE_PATTERNS)"
case " $patterns " in
    *" *.hpp "*) ;;
    *) fail "FILE_PATTERNS is '$patterns', it must name *.hpp" ;;
esac
case " $patterns " in
    *" *.cpp "*|*" *.cc "*|*" *.c "*) fail "FILE_PATTERNS is '$patterns', it must not name source files" ;;
esac

case "$(dox_get EXCLUDE)" in
    *json.hpp*) echo "PASS: the vendored header is excluded" ;;
    *) fail "EXCLUDE does not name the vendored json.hpp" ;;
esac

for tag in EXTRACT_PRIVATE EXTRACT_STATIC; do
    if [ "$(dox_get $tag)" != "NO" ]; then
        fail "$tag is '$(dox_get $tag)', it must be NO so private members are not documented"
    fi
done

# --- workflows -------------------------------------------------------------------------------------------
if [ -d "$srcdir/.github" ]; then
    wf="$srcdir/.github/workflows"
    hits="$(grep -nE 'contents: *write|git (commit|push)' "$wf"/*.yaml 2>/dev/null)"
    if [ -n "$hits" ]; then
        fail "a workflow can write to the repository:"
        echo "$hits" | sed 's/^/    /'
    else
        echo "PASS: no workflow has a write grant, a commit or a push"
    fi

    pages="$wf/doxygen.yaml"
    before=$nfail
    if [ ! -f "$pages" ]; then
        fail "the documentation workflow $pages does not exist"
    else
        # The workflow-level permissions must be empty or read-only.
        top="$(awk '/^permissions:/ { sub(/^permissions:[ \t]*/, ""); if ($0 != "") { print; exit } ; grab = 1; next }
                    grab && /^[^ \t#]/ { exit } grab && /:/ { print }' "$pages")"
        case "$top" in
            ""|"{}") ;;
            *) if echo "$top" | grep -v 'read' >/dev/null; then fail "doxygen.yaml workflow-level permissions are not read-only: $top"; fi ;;
        esac
        if ! grep -qE '^permissions: *\{\}' "$pages"; then
            fail "doxygen.yaml does not start from empty workflow-level permissions"
        fi
        # Repository contents may only ever be read; only the Pages grants may be write.
        if grep -nE '^[ \t]*[a-z-]+: *write' "$pages" | grep -vE '(pages|id-token): *write' >/dev/null; then
            fail "doxygen.yaml grants write access other than pages and id-token"
        fi
        if ! grep -qE 'contents: *read' "$pages"; then
            fail "doxygen.yaml build job does not state contents: read"
        fi
        if grep -nE '^[ \t]*pull_request' "$pages" >/dev/null; then
            fail "doxygen.yaml is triggered by pull requests"
        fi
        if grep -nE 'persist-credentials: *true' "$pages" >/dev/null; then
            fail "doxygen.yaml keeps the checkout credentials"
        fi
        if grep -nE 'secrets\.' "$pages" >/dev/null; then
            fail "doxygen.yaml uses a secret"
        fi
        [ "$nfail" -eq "$before" ] && echo "PASS: the documentation workflow is read-only"
    fi

    ci="$wf/ci.yaml"
    before=$nfail
    job="$(awk '/^  docs:/ { grab = 1; print; next } grab && /^  [A-Za-z0-9_-]+:/ { exit } grab { print }' "$ci" 2>/dev/null)"
    if [ -z "$job" ]; then
        fail "ci.yaml has no docs job"
    else
        echo "$job" | grep -qE 'contents: *read' || fail "the ci.yaml docs job does not state contents: read"
        if echo "$job" | grep -E ': *write|secrets\.|git (commit|push)|upload-pages-artifact|deploy-pages' >/dev/null; then
            fail "the ci.yaml docs job is not read-only"
        fi
        echo "$job" | grep -q 'make doxygen' || fail "the ci.yaml docs job does not run make doxygen"
        echo "$job" | grep -q 'ECS_REQUIRE_DOCS=1' || fail "the ci.yaml docs job does not require the documentation check"
        [ "$nfail" -eq "$before" ] && echo "PASS: the ci.yaml docs job is read-only"
    fi
else
    echo "SKIP: no .github directory in the source tree; workflow assertions skipped"
fi

# --- generated site --------------------------------------------------------------------------------------
if ! command -v doxygen >/dev/null 2>&1; then
    if [ "${ECS_REQUIRE_DOCS:-0}" = "1" ]; then
        echo "FAIL: doxygen is required but not installed"
        exit 1
    fi
    echo "SKIP: doxygen is not installed; the generated site was not checked"
    [ "$status" -ne 0 ] && exit "$status"
    exit 77
fi

out="$tmp/out"
mkdir -p "$out"
cp "$doxyfile" "$tmp/Doxyfile"
printf 'OUTPUT_DIRECTORY = %s\nQUIET = YES\n' "$out" >>"$tmp/Doxyfile"
if ! (cd "$builddir" && doxygen "$tmp/Doxyfile") >"$tmp/doxygen.log" 2>&1; then
    fail "doxygen exited with an error"
    tail -n 20 "$tmp/doxygen.log" | sed 's/^/    /'
    exit 1
fi
echo "PASS: doxygen ran"

html="$(find "$out" -name 'classecs_1_1Container.html' -exec dirname {} \; 2>/dev/null | head -n 1)"
if [ -z "$html" ]; then
    fail "no documentation page was generated for ecs::Container"
    html="$out/html"
fi
for name in Container Entity Component System Manager Timing Clock Uuid; do
    [ -f "$html/classecs_1_1$name.html" ] || fail "no class page for ecs::$name"
done
[ -f "$html/structecs_1_1Resource.html" ] || fail "no struct page for ecs::Resource"

unwanted="$(find "$out" -type f | sed "s|^$out/||" | awk -F/ '{ print $NF }' |
            grep -iE 'json|nlohmann|test_|example|bench_' | sort -u)"
if [ -n "$unwanted" ]; then
    fail "the site contains pages for excluded files:"
    echo "$unwanted" | sed 's/^/    /'
fi

if [ -f "$html/classecs_1_1Container.html" ] && grep -q 'id="pri-methods"' "$html/classecs_1_1Container.html"; then
    fail "the Container page documents private members"
fi

if [ "$status" -eq 0 ]; then
    echo "PASS: the generated site has the expected pages and nothing excluded"
fi
exit "$status"
