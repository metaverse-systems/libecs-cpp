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

## Changing systems and timers while they run

Systems and timers can be added, removed and cleared from inside `Initialize()`, `Update()` and timer
callbacks. The rules below describe what happens.

### Terms

* A **walk** is the start-up sequence (`Container::SystemsInitialize()`), an update pass
  (`Container::Update()`), or one system's timer walk (the part of `System::UpdateSystem()` that fires
  due timers before the system's own `Update()`).
* The **effect point** of a container walk is the moment it returns, normally or because a system threw.

### Removing systems

* `SystemDestroy`, `EntityDestroy` and `ComponentDestroy` accept any identifier, including a field of the
  object being removed, such as `SystemDestroy(system->Handle)`.
* An identifier that is not registered, including `""`, is a silent no-op. Removing a system twice
  releases it once.
* A removed system stops being part of the world when the call returns: it is not in `Systems`, is not
  exported, cannot receive messages, and is never started, updated or timer-walked again, even later in
  the same pass. Its identifier may be registered again at once.
* A system removed during a walk, or from inside its own timer walk, stays in memory until the effect
  point. Code running inside it may finish, but must not use the system after the walk ends. A system
  removed from its own timer walk in a direct `UpdateSystem()` call stays in memory until the end of the
  next container walk or until the container is destroyed. A system removed outside any walk is destroyed
  at once.
* If a timer callback removes its own system, no further timers of that system fire and its `Update()`
  does not run in that pass.

### Adding systems

* `Container::System()` registers the system at once; it is in `Systems` when the call returns.
* A system registered during an update pass is not updated in that pass. A system registered during
  start-up does not get `Initialize()` from that sequence. In both cases it is updated from the next pass,
  after every system registered before it.
* A system registered under the identifier of a system removed in the same pass is a new system. It goes
  to the end of the order, is not updated in that pass, and is not affected when the removed system is
  released.

### Order

* Systems are updated in registration order, each at most once per pass. Adding or removing systems never
  changes the relative order of the others.
* An update pass in which nothing is added or removed allocates no memory and does not copy the system
  list.

### Timers

* A timer callback may call `TimerAdd` on its own system or on any other system of the same world. A timer
  added to a system during that system's timer walk does not fire in that walk; it is considered from the
  system's next update.
* A timer callback may call `TimerClear(name)` on its own system, including for its own name. Every timer
  with that name is cancelled, and a cancelled timer that has not been reached yet does not fire.
* After a timer walk, exactly the one-shot timers that fired in it are removed. A one-shot timer that adds
  a new timer with its own name keeps the new timer.
* Cancelling and then adding the same name in one callback leaves exactly the new timer. Adding and then
  cancelling removes both.
* Timer changes made by a callback take effect before the system's own `Update()` runs in the same pass.
  `TimerAdd` and `TimerClear` called from `Update()` itself take effect at once.

### Failures

* If `Initialize()`, `Update()` or a timer callback throws, the error is logged at level `error` with the
  system's registered identifier and rethrown to the caller of `Update()` or `SystemsInitialize()`. On a
  world's own thread the exception is caught at the thread boundary and the manager is asked to shut down.
* Before the exception leaves the walk, every change requested in it completes. Timer additions and
  cancellations made before the failure are kept, every one-shot timer that fired (including the one
  whose callback threw) is removed, timers not yet reached do not fire, systems removed earlier stay
  removed and are released once, and systems added earlier stay registered. A caller that catches the
  exception and calls `Update()` again sees a consistent world.

### Examples

* **One-shot re-arm under the same name.** A one-shot timer named `retry` fires and calls
  `TimerAdd` with a new timer named `retry`. After the walk the fired timer is removed and the new one is
  kept, so `retry` fires again later.
* **Removing a system twice in one pass.** A system calls `SystemDestroy` twice with the same identifier
  in one pass, either for a sibling or for itself. The first call removes it, the second is a no-op, and
  the system is released once when the pass returns.
* **Removing and re-registering an identifier in one pass.** A system `a` is removed and a new system is
  registered as `a` in the same pass. The new system is not updated in that pass, goes to the end of the
  order, and is updated from the next pass. Releasing the old one at the end of the pass does not touch it.
* **A callback that changes timers and then throws.** A callback calls `TimerAdd` for a timer `x`,
  `TimerClear` for a timer `y`, and then throws. The exception reaches the caller of `Update()`, but
  `x` is kept, `y` is gone, the callback's own one-shot timer is removed, and timers after it did not fire.

### Not covered

* Changing a world from any thread other than the one driving its walks.
* `Container::Export()` and the `System::Export()` overrides it calls are a `const` query. They must not
  add or remove systems, entities or components.
* Calling `Update()` or `SystemsInitialize()` from inside a walk of the same container is memory safe, but
  no outcome is defined.
* Notifying systems when they are removed, and starting systems that are added late.
* Registering a second system under an identifier that is still in use. The previous outcome is kept and
  is memory safe.

### Release notes

#### 1.2.0

The rules above are now guaranteed. The object layout of `ecs::Container`, `ecs::System` and
`ecs::Timer` changed (private members only, no public signature changed), so plugins must be rebuilt
against the new headers. `the-seed build` does this.

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
For it to find the Windows build of Catch2, keep `PKG_CONFIG_PATH` set to the prefix's `lib/pkgconfig`
and `share/pkgconfig` directories while running `configure` and `make check`.
