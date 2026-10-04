# Working on libecs-cpp

This document is for people who change the library: how to run the tests, what each test checks, and what
the hosted checks do. To build and use the library, read the [README](README.md).

## Running the tests

The tests need Catch2 v3; the package to install is in the [requirements](README.md#requirements).

```
./autogen.sh
./configure --enable-tests=yes
make
make check
```

* `--enable-tests=yes` makes `configure` stop if Catch2 is missing. Without it, `configure` skips the tests
  when Catch2 is not found, and `make check` then fails with a message that says why.
* `--enable-werror=yes` treats warnings as errors, as the hosted checks do.
* `npm test` runs the same `make check`. It runs `./autogen.sh` and `./configure` first when they have not
  been run, and exits non-zero when a test fails.
* `make check TESTS=test_Manager` runs one program or script.

`make check` builds and runs every test in `tests/` and finishes in a few seconds. Each test prints one
`PASS:` or `FAIL:` line, followed by a summary (`# TOTAL`, `# PASS`, `# FAIL`, `# ERROR`). The command
exits non-zero if any test fails, crashes or cannot start, and the `FAIL:` line names the test. The full
output of each test is in `tests/<name>.log`, and the combined output of a failing run is in
`tests/test-suite.log`.

### Test programs

| Program | What it checks |
|---|---|
| `test_Manager` | Creating and finding containers |
| `test_Container` | Containers: entities, systems, export |
| `test_Entity` | Entities and their components |
| `test_System` | Systems, timers and changes during a pass |
| `test_Timing` | Schedule arithmetic, with explicit instants |
| `test_Elapsed` | Elapsed time, the first update, stalls and the container clock, on a controlled clock |
| `test_Compatibility` | The deprecated names give their earlier results |
| `test_UpdateAllocation` | A pass without changes allocates nothing |
| `test_Uuid` | Generating and parsing identifiers |
| `test_Threading` | Messages, deferred functions and logging from several threads |
| `test_Validation` | Rejected messages, systems and components |
| `test_Lifecycle` | Start-up, shutdown and stopping |
| `test_ProcessManager` | The process-wide manager, `ECS` |
| `test_Access` | Component lookup and ownership |
| `test_Logging` | Levels, held lines, colour, library warnings, handles set at construction |
| `test_Resources` | Resources |
| `test_Export` | JSON export |

### Test scripts

| Script | What it checks |
|---|---|
| `run-test-selftest.sh` | The wrapper that runs each test program and applies the known gaps |
| `run-example.sh` | `src/example.cpp` runs and shuts down cleanly |
| `run-logging.sh` | Nothing is written to the console outside the default log destination; output to a file and to a terminal |
| `check-exports.sh` | The names the shared library exports; see [Exported names](#exported-names) |
| `check-exports-selftest.sh` | `check-exports.sh` reports a leaked name and accepts a clean object |
| `check-style.sh` | Member naming: no trailing underscores, camelCase private members |
| `check-style-selftest.sh` | `check-style.sh` reports a wrong name |
| `check-install.sh` | Installs into a temporary directory, compares the file list with the expected one, then uninstalls |
| `check-headers.sh` | Every installed header compiles alone, twice, and with `ecs.hpp` in either order |
| `check-consumer.sh` | Small programs build and run with the installed `pkg-config` flags only |
| `check-docs.sh` | What the API documentation covers, and that the workflows only read the repository. It is skipped when `doxygen` is missing, and fails instead when `ECS_REQUIRE_DOCS=1` is set. |
| `check-readme.sh` | The files, options and packages that the documents name exist, and the required sections are present |

## Sanitizer variants

Two opt-in variants run the same tests with a sanitizer built in:

```
make distclean
./configure --enable-sanitizer=address
make
make check
```

```
make distclean
./configure --enable-sanitizer=thread
make
make check
```

* `address` enables AddressSanitizer together with UndefinedBehaviorSanitizer: memory errors, leaks and
  undefined behavior. It corresponds to the hosted check `test (address+undefined)`.
* `thread` enables ThreadSanitizer: data races. It corresponds to the hosted check `test (thread)`.
* The sanitizer runtime options are fixed in `tests/Makefile.am`, so a local run behaves the same as the
  hosted one.
* Run `make distclean` before switching variants.

## Known gaps

`tests/known-gaps.txt` lists sanitizer findings that are already understood and are waiting for a planned
fix. Each entry names the sanitizer variant, the test program, the test case, a text that must appear in
the failure output, the finding, and the roadmap task that fixes it. The file currently has no entries.

| Situation | Result |
|---|---|
| A listed test case fails with the listed text | The run passes and prints a `KNOWN GAP:` line. |
| Any other failure, in a listed or unlisted test case | The run fails. |
| A listed test case passes | The run prints a `STALE KNOWN GAP:` line. Remove the entry. |
| An entry is malformed or names a test case that does not exist | The run fails with a `MALFORMED KNOWN GAP:` line. |

Entries are removed when the fix lands. The file is never used to hide a new defect.

## Testing a Windows build

In a [Windows cross-build](README.md#building-for-windows), `make check` builds the test programs
(`tests/*.exe`) but does not run them. It runs the checks that need no Windows program:
`check-install.sh`, `check-headers.sh`, `check-consumer.sh` (which compiles and links its consumers but
does not run them), `check-docs.sh` and `check-readme.sh`.

For `configure` to find the Windows build of Catch2, keep `PKG_CONFIG_PATH` set to the prefix's
`lib/pkgconfig` and `share/pkgconfig` directories while running `configure` and `make check`.

## Exported names

The shared library exports `ECS`, the `ecs::` names declared in the installed public headers and the
reserved names the toolchain adds. It exports nothing else: the default log destination and its helpers
have internal linkage.

`tests/check-exports.sh` enforces the rule on a built library. It lists the defined dynamic symbols with
`nm -D --defined-only -C`, prints each offending name and exits non-zero. It fails on:

* a global-namespace symbol that `ecs.hpp` does not declare;
* an `ecs::` name that no public header declares;
* a class or function defined in a public header that has no symbol in the library. Types that are only
  code in a header are listed in the script.

## Documentation

The documentation is four Markdown files and the comments in the public headers:

| File | Content |
|---|---|
| [README.md](README.md) | What the library is, how to build, install and link it, and a first program |
| [GUIDE.md](GUIDE.md) | A program built step by step |
| [REFERENCE.md](REFERENCE.md) | The behaviour of the library, by topic |
| [NEWS.md](NEWS.md) | The versioning rule and the release notes |

`make doxygen` writes the API documentation to `doxygen/html` in the build directory, from the public
headers and these files. It needs `doxygen` and `graphviz`. The generated site is not stored in the
repository.

In a header comment, the first sentence is the one-line description that the site shows in its lists, so
it says what the member does. The last line of a member's comment states its
[thread category](REFERENCE.md#threading).

## Continuous integration

Every pull request to `master`, every push to `master` and every manual run builds the library and runs
`make check` as four separate checks:

| Check | Variant |
|---|---|
| `test (plain)` | No sanitizer |
| `test (address+undefined)` | `--enable-sanitizer=address` |
| `test (thread)` | `--enable-sanitizer=thread` |
| `test (plain (arm64))` | No sanitizer, on a 64-bit ARM machine |

These runs have read-only access to the repository and no secrets, so pull requests from forks are checked
the same way as pull requests from this repository. A manual run accepts a `repeat` count to run the tests
several times in a row.

A read-only `docs` check builds the API documentation for every pull request. The site is published to
GitHub Pages only by pushes to `master` and by a manual run of the Publish Documentation workflow, never
by pull requests. The repository owner enables publishing once, under Settings, Pages, Build and
deployment, Source: GitHub Actions. Until then the publishing job reports an error and the site is not
updated.

## Releases

The rule for the package version and for the shared-library version is in
[Versioning](NEWS.md#versioning). `tests/check-install.sh` names the expected library files, so a change
to the shared-library version and to that check go in the same commit.
