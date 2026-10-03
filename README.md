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

* `Container::Export()` and the `System::Export()` overrides it calls are a `const` query. They must not
  add or remove systems, entities or components.
* Calling `Update()` or `SystemsInitialize()` from inside a walk of the same container is memory safe, but
  no outcome is defined.
* Notifying systems when they are removed, and starting systems that are added late.
* Registering a second system under an identifier that is still in use. The previous outcome is kept and
  is memory safe.

## Identifiers

Every container, entity, component and system gets a text handle that the library generates from an
`ecs::Uuid`. `Get()` returns the text form of an identifier: 36 lowercase hexadecimal characters with
hyphens after the 8th, 12th, 16th and 20th, for example `550e8400-e29b-41d4-a716-446655440000`.
Generated identifiers are version 4 with the standard variant.

* **Thread safety.** `ecs::Uuid()` can be called from any number of threads at the same time, including
  threads the library did not create. Each thread has its own generator, seeded independently on that
  thread's first identifier, and no lock is taken.
* **Not secret.** Identifiers are unique in practice but are not cryptographically unpredictable. Do not
  use them as secrets or tokens.
* **Fork.** A child process that forks after generating identifiers continues the parent's sequence, so
  both processes can produce the same identifiers afterwards.
* **Parsing.** `ecs::Uuid(text)` accepts exactly 36 characters in upper or lower case, with hyphens in
  the standard places, and any version or variant (including the all-zero identifier). `Get()` always
  returns lowercase. Text saved by earlier versions parses and round-trips unchanged.
* **Rejected forms.** The empty string, text of any other length, leading or trailing whitespace, braces,
  a `urn:uuid:` prefix, the form without hyphens, and any character that is not a hexadecimal digit.
* **Errors.** Rejected text throws `std::runtime_error`. The message is a single line of printable ASCII
  and names the problem and, for a bad character, its 1-based position (the first one found). Long input is shortened to
  its first 64 characters and unprintable bytes are written as `\xNN`.

```
ecs::Uuid(""): text is empty
ecs::Uuid("550e8400-e29b-41d4-a716-44665544000"): text is 35 characters long, expected 36
ecs::Uuid("550e8400xe29b-41d4-a716-446655440000"): expected '-' at position 9, found 'x'
ecs::Uuid("550e8400-e29b-41d4-a716-44665544000g"): invalid character 'g' at position 36, expected a hexadecimal digit
```

The installed `ecs-cpp.pc` provides `-std=c++20 -pthread` in its compile flags. A consumer that wants a
later standard must put its own `-std=` after the pkg-config flags.

## Threading

A **world** is an `ecs::Container`. The **world thread** is the thread that runs the world's update
passes: its own background thread once `Start()` has been called, or the one thread an application uses
to call `Update()`. Any other thread is an **outside caller**.

The rule has three classes of operation:

* **Safe from any thread.** Delivering messages, using the manager, requesting and observing shutdown,
  replacing the log destination, logging, and handing the world a deferred change.
* **World thread only.** Everything that reads or changes entities, components, systems, resources,
  timers or the update order, and everything that reads `System::messages`.
* **Unsupported from outside callers of a running world.** Direct changes to entities, components,
  systems or resources. The outcome is undefined. The supported alternative is `Container::Defer()`.

A world that is not running (never started, and nobody is calling `Update()`) may be changed directly
from one thread, as before. Only one thread at a time may do so. `Start()` must be called once, by the
thread that owns the world, before any other thread uses it. Code inside a world (systems, timer
callbacks) has no new obligations: no locks and no new calls, and `Entities`, `Components`, `Systems`
and the update order stay usable from the world thread exactly as before.

### Operations

| Member | Class | Notes |
|---|---|---|
| `Manager::Container(handle)`, `Manager::Container()` | Any thread | One world per handle even when many threads ask at once. The pointer is valid until the manager is destroyed. |
| `Manager::ContainersGet()` | Any thread | A snapshot by value; worlds created later are not in it. |
| `Manager::IsRunning()`, `Manager::Shutdown()` | Any thread | Atomic. Once `IsRunning()` returns `false` it never returns `true` again. Idempotent. |
| `Manager::MessageSubmit(message)` | Any thread | Returns without waiting for the destination's update. Throws `std::runtime_error` if the world is unknown or the manager is being destroyed. |
| `Manager::~Manager()` | Exclusive | Waits for sends already in progress; later sends fail as unknown. The process-wide `ECS` manager is never destroyed. |
| `Container::Start()`, `Start(interval)` | Once | Call once, from the thread that owns the world, before any other thread uses it. |
| `Container::Defer(fn)` | Any thread | See "Deferred changes". |
| `Container::MessageSubmit(message)` | Any thread | Throws `std::runtime_error` if the system is unknown. |
| `Container::Log()`, `Container::LoggerSet()` | Any thread | Each line goes to exactly one destination. |
| `Container::UuidGet()`, `Handle`, `Manager` | Any thread | `Handle` and `Manager` never change after construction. |
| `Container::Update()`, `SystemsInitialize()` | World thread | One thread at a time. |
| `Container::System()`, `SystemDestroy()` | World thread | Register with `System()`; routing by handle depends on it. |
| `Container::Entity()`, `EntityDestroy()`, `Component()`, `ComponentDestroy()` | World thread | |
| `Container::ResourceAdd()`, `Resources()`, `ResourceGet()`, `Export()` | World thread | |
| `Container::Entities`, `Components`, `Systems` | World thread | Public so systems can iterate them. |
| `Container::~Container()` | Exclusive | Discards pending deferred changes unrun, stops and joins the thread. |
| `System::MessageSubmit(message)` | Any thread | The system must be alive. Routing through `Container` or `Manager` is safe against removal; a direct pointer is not. |
| `System::MessagesWaiting()`, `messages` | World thread | `MessagesWaiting()` moves delivered messages into the queue, then counts the messages waiting to be read. |
| `System::Initialize()`, `Update()`, `UpdateSystem()`, `Configure()`, `Export()`, `Shutdown()`, `TimerAdd()`, `TimerClear()`, `DeltaTimeGet()`, `Log()` | World thread | |
| `System::Handle`, `Container`, `Components`, `Timing` | World thread | Set during registration. |
| `Entity`, `Component` | World thread | They change the world's tables. |

### Messages

A message accepted by `MessageSubmit()` on `Manager`, `Container` or `System` is received by the
destination system exactly once while that system exists. Messages from one sender to one destination
arrive in the order sent; no order is promised between senders. Sending does not wait for any update pass
and does not call into the destination. A message becomes visible in `messages` at the start of the
destination's next `UpdateSystem()` (or earlier, if the world thread calls `MessagesWaiting()`). A message sent during a pass to a system later in the update order is
handled in that pass; a message to the sender itself, or to a system already updated in the pass, is
handled in the next pass. A message is never handled inside the sender's call.

If a system is removed while a message is on its way, the message is discarded with the system or the send
fails as unknown; it is never delivered to a destroyed object. An unknown destination throws
`std::runtime_error`, so callers on other threads should catch it.

The mailbox is unbounded. The library never drops a message to save memory, so an application that needs
back-pressure provides it.

### Deferred changes

`void Container::Defer(std::function<void()> fn)` hands a change to the world's own thread.

* It is safe from any thread, including from inside the world (a system, a timer callback or another
  deferred function).
* `fn` runs exactly once, on the world thread, at the start of the next `Update()`, before any system is
  updated and never in the middle of a pass.
* Functions run in the order they were accepted, so one submitter's functions run in submission order.
* Inside `fn` the usual rules for code inside a world apply, so every world-thread operation is available.
* A function submitted from inside `fn`, or during a pass, runs in the next pass.
* If `fn` throws, the error is logged at level `error`, the other functions in the batch still run, and
  the first error is rethrown by `Update()` before any system is updated. On a world's own thread it is
  caught at the thread boundary and the manager is asked to shut down, as for a system's error.
* A world that is never updated never runs its deferred functions. Functions pending when the world is
  destroyed, or submitted after destruction begins, are discarded without running.
* `fn` and everything it captures must stay valid until it runs or is discarded. A function that needs an
  entity looks it up by handle inside the function.

A command from the main thread to a running world:

```cpp
ecs::Manager manager;
ecs::Container *world = manager.Container("main");
world->Start();

// The world is running, so do not call world->Entity() from here.
world->Defer([world]() {
    ecs::Entity *player = world->Entity();
    // ... add components to player ...
});
```

Two worlds messaging each other:

```cpp
// In a system of world "a", on its world thread.
void Update() override
{
    // Received by "pong" in its next update (or the same pass if it comes later in the order).
    this->Container->Manager->MessageSubmit({
        {"destination", {{"container", "b"}, {"system", "pong"}}},
        {"payload", "ping"}});

    // To itself or to a sibling that was already updated in this pass: handled in the next pass.
    this->MessageSubmit({{"destination", {{"container", "a"}, {"system", "ping"}}}});
}
```

### Log destination

`LoggerSet(fn)` and `Log(message, level)` are safe from any thread at any time. Each `Log()` call goes to
exactly one destination that was installed at some time during the call, and a call that starts after
`LoggerSet()` returned uses the new destination. A destination may call `Log()` or `LoggerSet()` itself,
because no lock is held while it runs. An empty function makes later `Log()` calls succeed and print
nothing. The previous destination may still finish a call that began before the replacement, so it must
stay callable until then.

### Locks and deadlocks

The library uses five mutexes: the manager's container table, and each container's mailbox table, deferred
queue and log destination, plus one per system mailbox. Each is a leaf. A thread holds at most one of them
at a time, and none is held while user code runs (system methods, timer callbacks, message handlers, log
destinations, deferred functions, or destructors of user objects). Sending never waits for a world to
update; the only blocking wait on library state is `~Manager` waiting for sends in progress, and those never
block (destroying a world also joins its thread). As a result, worlds whose systems send messages to each
other in both directions, or to themselves, cannot deadlock. An application's own locks are its own
responsibility: do not hold one while calling a library function that your handlers also need under that
lock.

### Shutdown

`Manager::Shutdown()` and `Manager::IsRunning()` are atomic. A request is visible to other threads in the
next poll in practice. `IsRunning()` never goes back to `true`, and concurrent requests are idempotent. A
request from inside a system is a single store and cannot deadlock. Requesting shutdown does not stop world
threads or destroy worlds.

## Release notes

### 1.4.0

* New `Container::Defer()` hands a change to a running world's own thread. See "Threading".
* The layout of `System`, `Container` and `Manager` changed, so plugins and every consumer built against
  1.3.0 must be rebuilt against the new headers. `the-seed build` does this.
* Systems are addressable by routed messages only if they are registered through `Container::System()`.
  Code that inserts into `Container::Systems` directly must register with
  `Container::System(std::make_unique<...>(...))`.
* `System::MessagesWaiting()` now also counts messages that were delivered but not yet moved to the queue
  (it moves them first), and is for the world thread only. Code that called it from another thread to see a
  backlog should count on the world thread, or have the system publish its own counter.
* The error for a message to an unknown system names the container it was sent to, rather than reading the
  container from the message. The exception type is unchanged.
* A message sent during a pass is first handled at the destination's next update. Do not rely on
  same-call or same-pass handling.
* Changing a running world from outside threads was always a data race. Wrap such a change in
  `Container::Defer()`.
* `~Manager` waits for sends in progress and destroys its worlds from its body, which removes a race for
  embedding code that destroys a `Manager` while its world threads message each other.

### 1.3.0

* The object layout of `ecs::Uuid` changed (16 bytes, alignment 8), so plugins must be rebuilt against
  the new headers. `the-seed build` does this.
* `uuid_v4.h` and `endianness.h` are no longer installed, and installing removes any copies left in the
  prefix by an earlier version.
* `--disable-builtin-uuid` is retired. The library always uses its own identifier generator and no longer
  needs libuuid.
* Parsing identifier text is correct and strict, and throws `std::runtime_error` on malformed text.
* `ecs::Uuid::Get()` is `const`.
* `ecs-cpp.pc` carries `-std=c++20 -pthread`.
* The library no longer needs processor extensions and builds on 64-bit ARM.

### 1.2.0

The rules in "Changing systems and timers while they run" are now guaranteed. The object layout of
`ecs::Container`, `ecs::System` and `ecs::Timer` changed (private members only, no public signature
changed), so plugins must be rebuilt against the new headers. `the-seed build` does this.

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
`make check` as four separate checks: `test (plain)`, `test (address+undefined)`, `test (thread)` and
`test (plain (arm64))`, which runs the plain variant on a 64-bit ARM machine.
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
