# libecs-cpp - Entity Component System for C++

See ```src/example.cpp``` for a minimal example.

## Build environment setup
  
The build system and its dependencies are designed around Linux,
if you want to build on Windows you will need to use Windows Subsystem for Linux.

* Install base packages

```
sudo apt install build-essential libtool pkg-config curl git gawk
```

* Install wine and Windows dev packages

```
sudo su -
dpkg --add-architecture i386
apt update
apt install libz-mingw-w64-dev mingw-w64-x86-64-dev binutils-mingw-w64-x86-64 \
g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64 wine wine32 wine64 wixl osslsigncode \
mingw-w64-tools
exit
```

* Download std::thread implementation for mingw

```
sudo su -

curl -o /usr/x86_64-w64-mingw32/include/mingw.thread.h \
https://raw.githubusercontent.com/meganz/mingw-std-threads/master/mingw.thread.h

curl -o /usr/x86_64-w64-mingw32/include/mingw.invoke.h \
https://raw.githubusercontent.com/meganz/mingw-std-threads/master/mingw.invoke.h

curl -o  /usr/x86_64-w64-mingw32/include/mingw.mutex.h \
https://raw.githubusercontent.com/meganz/mingw-std-threads/master/mingw.mutex.h

exit
```

## Build library

* Build and install libecs-cpp

```
cd libecs-cpp
./autogen.sh
./configure
make
sudo make install
```

* Run the example

```
./src/example
```

## Running the tests

```
./autogen.sh
./configure
make
make check
```

`make check` builds and runs every test program in `tests/` and finishes in a few seconds. Each
program prints one `PASS:` or `FAIL:` line, followed by a summary (`# TOTAL`, `# PASS`, `# FAIL`,
`# ERROR`). The command exits non-zero if any program fails, crashes or cannot start, and the
`FAIL:` line names the program. The full output of each program is in `tests/<program>.log`, and the
combined output of a failing run is in `tests/test-suite.log`.

The tests need Catch2 v3 (`catch2-with-main` in pkg-config), for example `sudo apt install catch2`.

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

`address` enables AddressSanitizer together with UndefinedBehaviorSanitizer (memory errors, leaks and
undefined behavior); `thread` enables ThreadSanitizer (data races). They correspond to the hosted
`test (address+undefined)` and `test (thread)` checks. The sanitizer runtime options are fixed in
`tests/Makefile.am`, so a local run behaves the same as the hosted one. Run `make distclean` before
switching variants. Any other value for `--enable-sanitizer` stops `configure` with an error.

## Known gaps

`tests/known-gaps.txt` lists sanitizer findings that are already understood and are waiting for a
planned fix. Each entry names the sanitizer variant, the test program, the test case, a text that must
appear in the failure output, the finding, and the roadmap task that fixes it. The file currently has
no entries.

When a listed test case fails with the listed text, the run passes and prints a `KNOWN GAP:` line.
Any other failure, in a listed or unlisted test case, still fails the run. If a listed test case
passes, the run prints a `STALE KNOWN GAP:` line: remove the entry. If an entry is malformed or names a
test case that does not exist, the run fails with a `MALFORMED KNOWN GAP:` line. Entries are removed
when the fix lands; the file is never used to hide a new defect.

## Continuous integration

Every pull request to `master`, every push to `master` and every manual run builds the library and runs
`make check` as three separate checks: `test (plain)`, `test (address+undefined)` and `test (thread)`.
These runs have read-only access to the repository and no secrets, so proposals from forks are checked
the same way as proposals from this repository. A manual run accepts a `repeat` count to run the tests
several times in a row.

The Doxygen documentation is regenerated and committed only by pushes to `master`, never by proposals.

## Build library for Windows

* Build and install libecs-cpp

```
export PKG_CONFIG_PATH=/usr/x86_64-w64-mingw32/lib/pkgconfig/
cd libecs-cpp
./autogen.sh
make distclean
./configure --host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32
make
sudo make install
unset PKG_CONFIG_PATH
```

* Test

```
export MING_LIB=`ls  /usr/lib/gcc/x86_64-w64-mingw32/|grep posix|head -n1`
WINEPATH="/usr/lib/gcc/x86_64-w64-mingw32/${MING_LIB};/usr/x86_64-w64-mingw32/lib" wine64 src/example.exe
```

`make check` in a Windows cross-build builds the test programs (`tests/*.exe`) but does not run them.
