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
callbacks. The rules below describe what happens. When a system is started and when it is told to shut
down is described in "Start-up, shutdown and stopping".

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
* A system registered under the identifier of a system that is still registered replaces it. See "Input
  validation".

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
* Start-up and shutdown notifications, which are described in "Start-up, shutdown and stopping".

## Time: intervals and elapsed time

### Terms

* An **interval** is the time between two updates of a repeating schedule: a system's `Timing`, a world's
  `Start(interval)` and a repeating timer. A **timer length** is the interval of a timer: how long after it
  was added (or after it last fired) it next fires. **Elapsed time** is how much time passed between the
  previous update of a system and the current one.
* Every interval and every timer length is a `std::chrono` duration, for example
  `std::chrono::milliseconds(100)` or `std::chrono::hours(2)`. Internally the unit is microseconds. A
  larger interval means the schedule fires less often.

### Rules

* **Zero** means every pass: a system with an interval of zero is updated on every pass of its world, and a
  timer of length zero fires once on every update of its system (never more than once per update, even if
  its callback adds or clears timers).
* **The maximum** is `ecs::MAX_INTERVAL`, 100 years of 365.25 days. A negative value or one above the
  maximum is rejected with `std::runtime_error` and nothing is changed: `Timing::SetInterval()`,
  `System::TimerAdd()` and `Container::Start(interval)` all check it.
* **The first fire** of a new timer is one full length after it was added with `TimerAdd()`, and the first
  update of a new `Timing` is due one full interval after it was created or restarted (`Restart(now)`).
  A schedule that is asked right after creation says no.
* **The default** interval of a system is 33 333 microseconds, about 30 updates a second. The catch-up
  rule is unchanged: a schedule that is asked late moves forward by exactly the whole intervals that
  passed, so its average rate stays exact; if more than two whole intervals passed (after a long stall) it
  fires once and starts counting again from that moment.
* **The first update.** There is no earlier update to measure from, so `ElapsedGet()` reports the
  system's configured interval before the first update and during it, whenever the system was constructed
  or registered. A system whose interval is zero reports zero for its first update; code that divides by
  the elapsed time must treat that value specially or set an explicit interval.
* **Measured once per update.** The clock is read once at the start of each update, and that one reading
  decides both whether the system is due and what its elapsed time is. `ElapsedGet()` and
  `ElapsedSecondsGet()` return the same value however many times they are called in that update, from
  `Update()`, a timer callback or helper code. Reading them changes nothing, a system that never reads
  them loses no time, and the values of consecutive updates add up to the time the clock moved (to within
  the one microsecond that truncating the clock reading costs).
* **No clamp.** After a stall the whole stall is reported: a system that was not updated for five hours
  gets an elapsed time of five hours. Code that integrates over the step should cap it itself.
* **The clock.** Systems and worlds read an `ecs::Clock`. The default is the real steady clock. A clock is
  not owned by what uses it and must outlive it. `System::ClockSet()` and `Container::ClockSet()` replace
  it (the world's call sets the clock of every system it holds and of every system registered later); the
  schedule, timers and elapsed measurement of each affected system start again from the new clock's time.
  A null pointer selects the real steady clock again.
  The thread of a world that was started with `Start()` still waits on the real clock, so use a
  `ManualClock` with worlds that are driven by calls to `Update()`.

### Examples

```cpp
class Movement : public ecs::System
{
  public:
    Movement() : System("game/Movement")
    {
        this->TimerAdd(ecs::Timer("autosave", [this] { this->Save(); }, std::chrono::hours(2)));
    }
    void Update() override
    {
        // Seconds as a double. Cap the step yourself so that one long stall is not integrated in one go.
        double dt = std::min(this->ElapsedSecondsGet(), 0.25);
        // position += velocity * dt;
    }
    nlohmann::json Export() const override { return {}; }
    void Save() {}
};
```

Slow a system down with a longer interval: `this->Timing.SetInterval(std::chrono::milliseconds(100));`.

Testing time without sleeping, with a controlled clock:

```cpp
ecs::ManualClock clock;
Movement system;
system.ClockSet(&clock);
clock.Advance(std::chrono::hours(2));
system.UpdateSystem();            // the autosave timer fires; ElapsedGet() is two hours
```

A schedule on its own, with explicit instants:
`ecs::Timing t(std::chrono::seconds(7200)); t.Restart(std::chrono::microseconds(0));`
then `t.ShouldUpdate(std::chrono::minutes(48))` is `false`.

### Old names and new names

The old names keep their previous meaning and units and still compile, now with a deprecation notice that
names the replacement. They stay for at least the next minor release after 1.8.0; removal is not
scheduled in 1.8.0 and will be announced in release notes first.

| Old | New |
|---|---|
| `Timing.SetFrequency(us)` and `Timing.GetFrequency()` (the number is an interval in microseconds, not a frequency) | `Timing.SetInterval(std::chrono::microseconds(us))` and `Timing.GetInterval()` |
| `Timing(us)` | `Timing(std::chrono::microseconds(us))` |
| `Timer("name", callback, 30)` (a bare number of seconds; a floating-point count no longer compiles) | `Timer("name", callback, std::chrono::seconds(30))` |
| `uint32_t ms = DeltaTimeGet();` (whole milliseconds) | `ElapsedGet()` (microseconds) or `ElapsedSecondsGet()` (seconds as a `double`) |
| the protected member `lastTime` | nothing: it is no longer maintained, use `ElapsedGet()` |
| `Container::Start(us)` | unchanged and not deprecated; `Start(std::chrono::microseconds(us))` shows the unit |

## Start-up, shutdown and stopping

A system has two notifications. `Initialize()` is the start-up notification and `Shutdown()` is the
shutdown notification. Each is delivered exactly once per system, on every path by which a system comes
into a world or leaves it. Both are called on the world thread unless the table below says otherwise, and
`Container::Log()` works from both and from a system's destructor.

### Start-up

* A system is started before its first `Update()`, whichever way it arrived: registered before the world
  started, registered later from world-thread code (another system's `Initialize()` or `Update()`, a timer
  callback or a deferred function), registered in a world that the application drives with `Update()`,
  or registered as a replacement for another system. The start happens on the world thread.
* `Update()` runs the start-up step by itself, after the deferred functions and before the update walk,
  so calling `SystemsInitialize()` is optional. Calling it, once or several times, never starts a system
  twice.
* Systems started in one step are started in registration order. A system registered during the step is
  started by the next step, and is not updated in the pass that registered it. A system removed before the
  step reached it is never started.
* If `Initialize()` throws, the error is logged at level `error` with the system's identifier and
  rethrown after the step has finished its bookkeeping. The system counts as started: it is not started
  again, it is updated, and it receives `Shutdown()` when it leaves.

### Shutdown

* A system that was started receives `Shutdown()` exactly once before it is destroyed. A system that
  was never started never receives it. After the notification the system is not updated and receives no
  routed message.
* When a world stops or is destroyed, the systems are shut down in the reverse of registration order,
  with the world's tables and log destination still in place.
* An exception thrown from `Shutdown()` is logged at level `error` with the system's identifier and
  swallowed, on every path. The other systems are still notified and released, and nothing escapes a
  destructor.
* While a world is being torn down, a notification may remove a later system (it is notified once and
  released when the teardown walk ends, so after the systems still to be visited), may register a system (it is not started and is released without a notification), and may
  call `Container::Stop()` or `Manager::Shutdown()` (neither waits and neither repeats the teardown).
* A `Shutdown()` that blocks is waited for. The library does not abandon a notification.

The thread and the moment depend on the path:

| Path | Thread that runs `Shutdown()` | When |
|---|---|---|
| `SystemDestroy()` outside a pass | The world thread (the caller) | During the call, before the system is destroyed |
| `SystemDestroy()` inside a pass or a start-up step | The world thread | When the outermost pass or step ends |
| Replacement (registering under a used identifier) | The world thread | When the outermost pass or step ends, or during the call outside one |
| `Container::Stop()` on a world with its own thread | The world's own thread | After the pass in progress, before the thread ends |
| `Manager::Shutdown()`, world with its own thread | The world's own thread | After the pass in progress, before the thread ends |
| Destruction of a world with its own thread | The world's own thread | The destructor stops the thread first |
| `Container::Stop()` on a world driven by `Update()` | The thread that calls `Stop()` | Before `Stop()` returns |
| Destruction of a world driven by `Update()` | The thread that destroys the world | During the destructor |
| `~Manager()` | Each threaded world's own thread, then the destroying thread for worlds driven by `Update()` | The destructor shuts down first, then destroys the worlds |

### Starting a world

`Start()` and `Start(interval)` take effect once. The first call on a world that has never been started,
has not been asked to stop and belongs to a manager that is still running starts the world's thread. Every
other call returns normally and does nothing: no second thread, no second start-up of any system, and the
interval stays as it was. A world that has been stopped cannot be started again, and a world of a manager
that has shut down cannot be started at all; those two calls log one warning. Make a new world instead.

### Stopping a world

`Container::Stop()` is safe from any thread and can be called more than once.

* From a thread that does not belong to the world, it asks the world to stop after the pass in progress
  and returns after the world's thread has ended and every notification has been delivered. A world
  driven by `Update()` has no thread, so it is torn down on the calling thread before `Stop()` returns.
* From the world's own thread (a system, a timer callback, a deferred function or a `Shutdown()`), it only
  asks and returns at once; the stop completes when the pass ends. The same holds for a call made from
  the thread of a different world: it only asks, and a world driven by `Update()` is then torn down by
  its owner's later `Stop()` or by destroying it.
* A world driven by `Update()` that is stopped from inside one of its own passes is torn down at once,
  on the calling thread: its systems are shut down, including the one that made the call, and the rest of
  that pass skips them.
* The wait for a stop does not depend on the update interval. An idle world with a 5 second interval stops
  in milliseconds. The tick period is unchanged when no stop is requested.
* After a stop, `Update()` does nothing (on a world with its own thread, from the moment the stop is
  requested), `Defer()` is dropped, and a system registered later is never
  started.
* Destroying a world stops it the same way and then releases it. Destruction is exclusive: no other
  thread may use the world while it runs. A world driven by `Update()` is stopped by its owner, with
  `Stop()` or by destroying it.

### Manager shutdown and destruction

* `Manager::Shutdown()` from an application thread sets `IsRunning()` to `false`, asks every world that has
  its own thread to stop and returns after every world with its own thread has ended and delivered its notifications. After it
  returns, no thread of the manager runs and no system is updated. It is idempotent, and concurrent callers
  each return after the work is complete.
* `Manager::Shutdown()` from a world thread only requests the stop and returns at once, so a system can
  call it from `Update()` without waiting for itself, and two worlds can do it at the same time. The
  wait is done by the application thread's later call.
* Worlds driven by `Update()` are not stopped by the manager, because the library does not run application
  code on threads it does not own. Stop them with `Stop()` or destroy them.
* A world thread that fails (an exception from start-up or an update) logs the error, calls
  `Manager::Shutdown()`, which now stops every world of the manager that has its own thread, delivers its own notifications and
  ends.
* `~Manager()` runs `Shutdown()` first, while every world still exists, so notifications that message
  another world find it. It then closes the manager: later sends and creation of worlds fail with
  `std::runtime_error`. Then the worlds are destroyed.

### The process-wide manager

`ECS` is never destroyed, so the library cannot stop its worlds at process exit. A program that uses it
calls `ECS->Shutdown()` from its main thread before `main` returns. The library installs no automatic hook.

### Examples

A threaded world:

```cpp
ecs::Manager manager;
ecs::Container *world = manager.Container("main");
world->System(std::make_unique<MySystem>("my-system"));
world->Start();            // MySystem::Initialize() runs on the world thread, then updates begin
// ... the program runs ...
manager.Shutdown();        // blocks until MySystem::Shutdown() has run and the thread has ended
```

A world driven by the application:

```cpp
ecs::Manager manager;
ecs::Container *world = manager.Container("main");
world->System(std::make_unique<MySystem>("my-system"));
while (running)
{
    world->Update();       // starts any system not yet started, then updates
}
world->Stop();             // MySystem::Shutdown() runs here, on this thread
```

A program that uses the process-wide manager:

```cpp
int main()
{
    ecs::Container *world = ECS->Container("main");
    world->System(std::make_unique<MySystem>("my-system"));
    world->Start();
    // ... the program runs ...
    ECS->Shutdown();       // before main returns: stops every world and waits for it
    return 0;
}
```

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

### System and component identifiers

A system's identifier is `System::Handle`. It is a `const std::string` that the base constructor sets and
nothing can change afterwards: `ecs::System()` generates a UUID, and `ecs::System("name")` uses the name
given. A system has exactly the identifier its constructor gave it; there is no later assignment, so a
system cannot be named twice. A subclass names itself in its initialiser list:

```cpp
class Loader : public ecs::System
{
  public:
    Loader() : ecs::System("game/Loader") {}
    nlohmann::json Export() const override { return {}; }
};
```

An empty identifier is still refused when the system is registered.

A component has no identifier of its own. It is addressed by the entity it is attached to
(`EntityHandle`) and its `Type`, for example with `Entity::Component()` and `Container::ComponentGet()`.

## Logging

`Container::Log(message, level = "info")` and `System::Log(message, level = "info")` take the same
default severity: a call with no severity is `info`. The usual levels are `error`, `warning`, `info` and
`debug`; any other name is passed on as given.

**Default destination.** Each world starts with a destination that writes `[level] message`. Lines at
`error` and `warning` go to standard error and every other line goes to standard output.

**Colour.** The tag is coloured only when the stream it is written to is an interactive terminal. Output
redirected to a file or a pipe carries no escape sequences. The environment variable `NO_COLOR`, when set
to a non-empty value, turns colour off on a terminal too. The two streams are decided separately (a
terminal on standard output with standard error redirected is coloured on one and plain on the other).
The decision is made once, when the world is created. Redirecting a stream later does not change it.
On Windows a stream counts as a terminal when it is a real console that accepts virtual terminal
processing.

**Replacing the destination.** `LoggerSet(fn)` installs a function that receives exactly
`(message, level)`: plain text, with no colour and no other decoration. Colour belongs to the default
destination only. The rules for calling it from several threads are in "Log destination".

**Lines logged before registration.** A system may call `Log()` in its constructor, before it belongs to
a world. Those lines are held and delivered to the world's destination, in the order they were logged and
each with the system's identifier as a prefix, when the system is registered with `Container::System()`,
before the world starts the system. A registration that is refused delivers nothing and the lines stay
held. A destination that throws does not undo the registration and does not stop the remaining lines.
Known limit: held lines have no cap, so a system that logs without limit before it is registered holds
them all in memory.

**Library warnings.** The library writes nothing to the console outside the default destination. A
warning it raises itself, for example `componentsClear()` on a system that has no world, goes through the
system's `Log()` like any other line, so it reaches a replacement destination and is held when there is
no world yet.

## Exported names

The shared library exports `ECS`, the `ecs::` names declared in the installed public headers and the
reserved names the toolchain adds, and nothing else. The default log destination and its helpers have
internal linkage; before 2.0.0 the library also exported a global `loggerFunction`. `tests/check-exports.sh`
enforces the rule on a built library: it lists the defined dynamic symbols with `nm -D --defined-only -C`,
fails on a global-namespace symbol that `ecs.hpp` does not declare, fails on an `ecs::` name that no
public header declares, and fails when a class or function defined in a public header has no symbol in
the library (types that are only code in a header are listed in the script). It prints each offending name
and exits non-zero. `tests/check-exports-selftest.sh` checks the checker against small objects that
leak a name and objects that do not.

## Input validation

Message submission, system registration, component attachment and the `Entity` constructors check their
input before they change anything. A rejection throws a catchable `std::runtime_error`, leaves the library
exactly as it was (no mailbox gains a message, no table gains an entry, and a rejected message takes no
lock and touches no counter), and the world stays usable. Error text has the form
`<function>: <condition>.`, where the function is written like `ecs::Manager::MessageSubmit()` or
`ecs::Container("world")::System()`, and a wrong type is reported as `, got <type>`. A rejection from inside a
system's `Update()` can be caught there. An uncaught one follows the existing path for any exception: it is
logged with the system's identifier and rethrown by `Update()`.

### Messages

A message given to `Manager::MessageSubmit()` or `Container::MessageSubmit()` is a JSON object with a
`destination` object. `destination.system` is non-empty text. For `Manager::MessageSubmit()`,
`destination.container` is non-empty text as well. `Container::MessageSubmit()` goes to the world that
received the call and does not read `destination.container`. Any other field is delivered unchanged.
The checks run in the order of the table and stop at the first failure.

| Condition | Key phrase of the error | Manager | World |
|---|---|---|---|
| The message is not a JSON object | `message must be a JSON object`, then `got <type>` | yes | yes |
| No `destination` | `message.destination is missing` | yes | yes |
| `destination` is not an object | `message.destination must be a JSON object`, then `got <type>` | yes | yes |
| No `destination.container` | `message.destination.container is missing` | yes | no |
| `destination.container` is not text | `message.destination.container must be text`, then `got <type>` | yes | no |
| `destination.container` is `""` | `message.destination.container is empty` | yes | no |
| No `destination.system` | `message.destination.system is missing` | yes | yes |
| `destination.system` is not text | `message.destination.system must be text`, then `got <type>` | yes | yes |
| `destination.system` is `""` | `message.destination.system is empty` | yes | yes |

`<type>` is the JSON type name: `null`, `boolean`, `number`, `string`, `array`, `object`, `binary` or
`discarded`. A well-formed message for an unknown destination throws `Container <name> not found.` (the
world named in the message) or `System <name> not found.` (naming the world that was asked for the
system; when a message goes directly to a world, that is the world that received it, whatever its
`destination.container` says).

### Registering systems

| Condition | Key phrase of the error | After the call |
|---|---|---|
| Null pointer | `system is missing` | Unchanged. |
| `Handle` is `""` | `system handle is empty` | Unchanged; the passed system is destroyed. |
| The object is already registered in a world | `system "<handle>" is already registered` | Unchanged; the pointer is released and the object is not deleted, because it belongs to its world. |

Registering a system under the `Handle` of a system that is still registered **replaces** it. The world
keeps one system for that identifier, in the old one's position in the update order. The new instance is
started and updated once, receives messages sent after the call, and never sees messages that were waiting
for the old one, which are discarded with it. The old instance is not visited again; it stays in memory
until the outermost walk ends, and raw pointers to it must not be used after the call. A system's
`Container` member is set by registration only.

### Attaching components

`Container::Component()` and `Entity::Component()` reject the inputs below. `EntityHandle` is set by
`Entity::Component()` and read by `Container::Component()`.

| Condition | Key phrase of the error | After the call |
|---|---|---|
| Null component | `component is missing` | Unchanged. |
| `Type` is `""` | `component type is empty` | Unchanged; no entry for the type. |
| `EntityHandle` is `""` (world call only) | `component entity handle is empty` | Unchanged. |
| `EntityHandle` names no entity in the world (never created, destroyed, or misspelled) | `entity "<handle>" does not exist` | Unchanged. |

Both functions take a `std::unique_ptr<ecs::Component>` by value and nothing else. The world owns the
component from the moment of the call, including when the call is rejected: the component is then released
exactly once and the caller's handle is empty. An empty handle is reported with the entity's name
(`ecs::Entity("handle")::Component()`); the other rejections are made by the world and carry the world's
name (`ecs::Container("world")::Component()`). Creating an `Entity` with a null container throws
`ecs::Entity: container is missing`.

A second component of the same `Type` on the same entity **replaces** the first, so an entity has exactly
one component of each type. Code that holds a `shared_ptr` to the replaced component keeps a valid object.
The same type on different entities is kept for each.

## Component access and ownership

The example in `src/example.cpp` is the one-page demonstration of everything in this section: a system
that walks the positions, reads the velocity of each entity without creating anything, and skips an entity
that has none.

### Looking a component up

```cpp
const std::string positionType = "PositionComponent";
const std::string velocityType = "VelocityComponent";

// the component of one entity, as kind T; empty when there is none
if(auto velocity = this->Container->ComponentGet<VelocityComponent>(entity, velocityType))
{
    velocity->Apply(delta);
}
// without a kind, any stored component
std::shared_ptr<ecs::Component> any = world->ComponentGet(entity, velocityType);
// the same two calls on an entity, without the entity argument
bool has = e->ComponentHas(velocityType);
```

* `ComponentGet<T>(entity, type)` returns a `std::shared_ptr<T>`. It is empty when the entity has no
  component of that type, when the type has never been used, when the entity is unknown, when either string
  is empty, and when the stored component is not a `T`. It never throws and never changes the world.
* `ComponentHas(entity, type)` answers whether a component is stored under that type name. Only the name is
  compared, so the component's class does not matter. It never throws and never changes the world.
* Both calls take no lock and, given existing `std::string` arguments, allocate nothing. A short string
  literal (up to 15 characters) converts without allocating; a longer one allocates, so code that runs
  every pass should keep its type names in `std::string` constants.
* The result keeps its object valid if the component is later replaced or removed.
* The public table `Components[type][entity]` is unchanged and still inserts an empty entry for a type or
  entity it does not find, as `std::unordered_map::operator[]` does. To read without inserting, use
  `ComponentGet`, or `find()` or `at()` on the table.

### Attaching a component

```cpp
auto stored = e->Component(std::make_unique<PositionComponent>(config));
world->Component(std::move(unique));      // the handle is empty afterwards
```

The world becomes the sole owner of the component at the call. Raw pointers, the address of an object the
caller owns, `std::shared_ptr`s and copies of a handle that the caller keeps do not compile. A second
component of a type replaces the first; holders of the old one keep a valid object. See "Attaching
components" for the rejected inputs.

### Resources

```cpp
world->ResourceAdd("tiles", std::move(bytes));   // moved, not copied
if(auto tiles = world->ResourceGet("tiles"))     // empty when the name is unknown
{
    use(tiles->Data);                            // read-only, shared
}
```

`ResourceGet` returns a `std::shared_ptr<const ecs::Resource>` that shares the stored bytes without a copy.
An unknown name gives an empty pointer; nothing is created and nothing is thrown. The data stays valid for
as long as the pointer is held, even after the resource is replaced or the world is destroyed. Treat a
resource as read-only once it has been added.

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
| `Manager::IsRunning()` | Any thread | Atomic. Once it returns `false` it never returns `true` again. |
| `Manager::Shutdown()` | Any thread | Idempotent. From an application thread it blocks until every world with its own thread has stopped and delivered its notifications; from a world thread it only requests. See "Start-up, shutdown and stopping". |
| `Manager::MessageSubmit(message)` | Any thread | Returns without waiting for the destination's update. Throws `std::runtime_error` if the message is malformed, the world or system is unknown, or the manager is being destroyed. A malformed message is rejected before any lock is taken. |
| `Manager::~Manager()` | Exclusive | Shuts down first, waits for sends already in progress; later sends and new worlds fail. The process-wide `ECS` manager is never destroyed. |
| `Container::ClockSet(clock)` | World thread | Every system of the world uses the new clock; see "Time: intervals and elapsed time". |
| `Container::Start()`, `Start(interval)` | Once | Call from the thread that owns the world, before any other thread uses it. A repeated call, a call after a stop and a call after manager shutdown do nothing. |
| `Container::Stop()` | Any thread | From the world thread it only requests; elsewhere it returns once the world has been torn down. |
| `Container::Defer(fn)` | Any thread | See "Deferred changes". |
| `Container::MessageSubmit(message)` | Any thread | Throws `std::runtime_error` if the message is malformed or the system is unknown. The world name in the message is ignored. |
| `Container::Log()`, `Container::LoggerSet()` | Any thread | Each line goes to exactly one destination. |
| `Container::UuidGet()`, `Handle`, `Manager` | Any thread | `Handle` and `Manager` never change after construction. |
| `Container::Update()`, `SystemsInitialize()` | World thread | One thread at a time. |
| `Container::System()`, `SystemDestroy()` | World thread | Register with `System()`; routing by handle depends on it. A second system under a used identifier replaces the first. |
| `Container::Entity()`, `EntityDestroy()`, `Component()`, `ComponentDestroy()` | World thread | |
| `Container::ComponentGet()`, `ComponentHas()`, `Entity::ComponentGet()`, `Entity::ComponentHas()` | World thread | Take no lock and change nothing. |
| `Container::ResourceAdd()`, `Resources()`, `ResourceGet()`, `Export()` | World thread | |
| `Container::Entities`, `Components`, `Systems` | World thread | Public so systems can iterate them. |
| `Container::~Container()` | Exclusive | Discards pending deferred changes unrun, stops and joins the thread, delivers `Shutdown()` to every started system. |
| `System::MessageSubmit(message)` | Any thread | The system must be alive. Routing through `Container` or `Manager` is safe against removal; a direct pointer is not. |
| `System::MessagesWaiting()`, `messages` | World thread | `MessagesWaiting()` moves delivered messages into the queue, then counts the messages waiting to be read. |
| `System::Initialize()`, `Update()`, `UpdateSystem()`, `Configure()`, `Export()`, `Shutdown()`, `TimerAdd()`, `TimerClear()`, `ElapsedGet()`, `ElapsedSecondsGet()`, `ClockSet()`, `Log()` | World thread | Time reads change nothing. The older `DeltaTimeGet()` is in the table of old names. |
| `System::Container`, `Components`, `Timing` | World thread | Set during registration. |
| `System::Handle` | Any thread | Given by the constructor and never changes. |
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
fails as unknown; it is never delivered to a destroyed object. A malformed or unknown destination throws
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

The library uses six mutexes: the manager's container table, each container's mailbox table, deferred
queue, log destination and start/stop state (`lifecycleLock`), and one per system mailbox. Each is a leaf.
A thread holds at most one of them
at a time, and none is held while user code runs (system methods, timer callbacks, message handlers, log
destinations, deferred functions, or destructors of user objects). Sending never waits for a world to
update; the blocking waits on library state are `~Manager` waiting for sends in progress (those never
block), and an application thread waiting for a world's thread to end in `Stop()`, `Manager::Shutdown()` or a
destructor. Code on a world thread never waits: a request from there to stop a world or the manager only
records the request. As a result, worlds whose systems send messages to each
other in both directions, or to themselves, cannot deadlock. An application's own locks are its own
responsibility: do not hold one while calling a library function that your handlers also need under that
lock.

### Shutdown

`Manager::Shutdown()` and `Manager::IsRunning()` are atomic. A request is visible to other threads in the
next poll in practice. `IsRunning()` never goes back to `true`, and concurrent requests are idempotent. A
request from inside a system only records the stop and cannot deadlock. A request from an application
thread also stops every world that has its own thread and waits until it has ended, as described in
"Start-up, shutdown and stopping". It does not destroy worlds.

## Release notes

### 2.0.0

Consistent logging, a system identifier that cannot change and a smaller public surface. This is a major
release: the layout of the base classes changes, and `System::Handle` can no longer be assigned.

**Rebuild every system plugin and every component plugin** (`the-seed build`). Removing
`Component::Handle` shrinks the `Component` base class, and `System::Handle` is now `const`. Plugins
compile these base classes into themselves, so a plugin built against 1.8.0 and loaded by 2.0.0 is not
supported: its behaviour is undefined. Loading was tried with minimal plugins. A component plugin built
against 1.8.0 does not load, because the constructor it calls is no longer exported by the library. A
system plugin that reads a component's `Type` and `EntityHandle` reads them from the old, shifted
positions and the program crashed. A system plugin that does nothing with components happened to run, but
that is luck of its layout and not something to rely on. Projects require `ecs-cpp >= 2.0.0`; libthe-seed
0.3.3 and the project templates of the-seed 1.8.0 do, and `configure` refuses a 1.8.0 install.

The shared-object name stays `libecs-cpp.so.0`. No libtool version-info is set for this library, so the
loader cannot tell 1.8.0 and 2.0.0 apart by name; the versioning of the shared object is planned
separately. Until then the rebuild rule above and the `ecs-cpp >= 2.0.0` requirement are what keep the
two apart.

Changed:

* `Component::Handle` is removed. A component has no identifier of its own; address it by its entity and
  type. The two `Component` constructors are defined in the header and the library no longer builds a
  `Component.cpp`.
* `System::Handle` is a `const std::string`. It is set only by the base constructor `System(handle)`
  (or generated by `System()`), and cannot be assigned afterwards.
* `System::Log(message, level)` takes `level = "info"`, the same default as `Container::Log`.
* Lines a system logs before it is registered are delivered to the world's destination at registration,
  in order, before the world starts the system. Before, they waited inside the system until its next
  `Log()` call after registration, and a system that never logged again never delivered them.
* The default destination colours a stream only when it is an interactive terminal, and not when
  `NO_COLOR` is set to a non-empty value. Redirected output is plain text. Before, every line carried
  escape sequences.
* A warning from the library itself (`componentsClear()` with no world) goes through the log destination
  instead of straight to standard output.
* Exported names: the global `loggerFunction` is no longer exported. Only `ECS` and the public `ecs::`
  names are, and `tests/check-exports.sh` keeps it that way.
* The private `Container::system_order` is now `systemOrder`.

Migration:

| Before | After |
|---|---|
| `Foo() { this->Handle = "name"; }` in a system | `Foo() : ecs::System("name") {}` |
| `system->Handle = "name";` after construction | Not possible. Pass the name to the constructor. |
| Reading `component->Handle` | No replacement. Address a component by `EntityHandle` and `Type`. |
| `this->Log("text", "info")` | Unchanged. `this->Log("text")` now means `info`. |
| A log destination that stripped colour codes | Remove the stripping. A replacement destination receives plain text. |
| Relying on colour in a redirected file | Gone. Colour appears on a terminal only; `NO_COLOR` turns it off there too. |
| Lines logged in a constructor arriving late | They now reach the destination at registration. |
| System and component plugins built against 1.8.0 | Rebuild all of them with `the-seed build`. |
| `PKG_CHECK_MODULES([LIBECS], [ecs-cpp >= 1.8.0])` | `ecs-cpp >= 2.0.0`; the soname is unchanged (`libecs-cpp.so.0`). |

Tests: `test_Logging` (default severity, held lines, colour decisions, the library warning and
construction-time identifiers), `test_Resources` and `test_Export` are new, as are the script tests
`run-logging.sh`, `check-exports.sh`, `check-exports-selftest.sh`, `check-style.sh` and
`check-style-selftest.sh`.

### 1.8.0

Clear timer lengths and intervals, trustworthy elapsed time and a clock that tests can control. This is a
minor release although `System` and `Timing` change layout, following the precedent of 1.4.0 to 1.7.0;
the migration of each change is below, and the rules are in "Time: intervals and elapsed time".

**Rebuild every system plugin** (`the-seed build`). `System::Timing` is a public member and its code is
compiled into each system plugin, and `System` gained members, so a system plugin built against 1.7.0 and
loaded by 1.8.0 is not safe. Loading one was tried: the plugin allocates the old, smaller object, the
1.8.0 constructor writes past its end, and the program corrupts memory and then crashes. A plugin that
only defines components (`create_component`) contains none of this code and keeps working without a
rebuild; that was checked by loading one built against 1.7.0 into 1.8.0. `Container::Start(unsigned int)`
and `System::DeltaTimeGet()` are still exported, so code rebuilt in place still links. libthe-seed 0.3.2
requires `ecs-cpp >= 1.8.0`.

New:

* `ecs::Clock`, `ecs::SteadyClock` (the default) and `ecs::ManualClock`, in `<libecs-cpp/Clock.hpp>`,
  with `System::ClockSet()` and `Container::ClockSet()`.
* `System::ElapsedGet()` (microseconds) and `System::ElapsedSecondsGet()` (a `double` in seconds).
* `Timing` takes `std::chrono::microseconds`: `SetInterval()`, `GetInterval()`, `ShouldUpdate(now)`,
  `Restart(now)`; the constants `ecs::MAX_INTERVAL` and `ecs::DEFAULT_INTERVAL`.
* `Timer` takes a `std::chrono` duration, and `Container::Start(std::chrono::microseconds)`.
* Out-of-range values are rejected with `std::runtime_error` instead of wrapping around.

Fixed:

* A timer length of more than about 71 minutes used to wrap around in a 32-bit count of microseconds: a
  two-hour timer fired after about 48 minutes. Lengths up to `MAX_INTERVAL` are now exact.
* Elapsed time no longer rounds a sub-millisecond gap down to zero or drops the remainder of each step,
  and no longer depends on how often it is read.

Tests: `test_Elapsed` (elapsed time, the first update, stalls and the world clock, all on a controlled
clock) and `test_Compatibility` (every deprecated name still gives its previous result) are new.

Migration:

| Old | New |
|---|---|
| `Timing.SetFrequency(us)` / `GetFrequency()` | `Timing.SetInterval(std::chrono::microseconds(us))` / `GetInterval()` |
| `Timing(us)` | `Timing(std::chrono::microseconds(us))` |
| `Timer("n", cb, 30)` (seconds; a floating-point count no longer compiles) | `Timer("n", cb, std::chrono::seconds(30))` |
| `uint32_t ms = DeltaTimeGet();` | `ElapsedGet()` (microseconds) or `ElapsedSecondsGet()` (seconds, `double`) |
| `Start(us)` | unchanged, or `Start(std::chrono::microseconds(us))` |

The deprecated names compile with a notice and keep their previous units and meaning, apart from the
behaviours below. A floating-point timer length (`Timer("n", cb, 0.5)`) no longer compiles: pass a duration.

Three behaviours to know about:

* `DeltaTimeGet()` used to return the time since the previous call. It now returns the time of the
  current update, measured once. A system that read it only every Nth update used to see N updates of
  time and now sees one, and a second call in the same update no longer returns about zero. Accumulate in
  the system, or switch to `ElapsedGet()` and sum the values you need.
* `DeltaTimeGet()` now carries the part of a millisecond that it cannot show into the next update, so the
  running total stays within a millisecond of the real one. A 33 333 microsecond step reads mostly 33 with
  a 34 about every third update, where it used to read 33 every time.
* `DeltaTimeGet()` and `Timing::GetFrequency()` hold 32 bits and saturate at 4 294 967 295 (about 49.7 days
  of milliseconds, and about 71 minutes of microseconds) instead of wrapping. `ElapsedGet()` and
  `GetInterval()` are never limited.

### 1.7.0

Component lookups that do not change the world, a sole-ownership attach and shared read-only resources.
This is a minor release although several changes are source-incompatible, following the precedent of 1.4.0
to 1.6.0; the migration of each is below. Plugins that call a changed function must be rebuilt
(`the-seed build`); plugins that only define a component class and `create_component` keep working without
a rebuild, because the changed functions are not part of what they call. This was checked by loading both
kinds of plugin, built against 1.6.0, into 1.7.0: the factory-only plugin loaded and worked, and the plugin
that called `Entity::Component(new ...)` failed with an undefined symbol.

New:

* `Container::ComponentGet<T>(entity, type)` and `ComponentHas(entity, type)`, and the same two on
  `Entity` without the entity argument. See "Component access and ownership".
* `src/example.cpp` no longer reads through the table: it looks velocities up with `ComponentGet` and
  skips an entity that has none.

Source-incompatible changes and their migration:

| Old | New |
|---|---|
| `e->Component(new T(cfg))` | `e->Component(std::make_unique<T>(cfg))` |
| `e->Component(loader.Create(...).release())` | `e->Component(loader.Create(...))`, passing the `std::unique_ptr` itself |
| `world->Component(shared)` | `world->Component(std::move(unique))` |
| `(*Components)["T"][entity]` to read a component | `Container->ComponentGet<T>(entity, "T")`, then test the result for empty |
| `auto r = world->ResourceGet(name);` (a copy of the bytes, throws for an unknown name) | `auto r = world->ResourceGet(name);` is a `std::shared_ptr<const ecs::Resource>`: test it for empty and read through `->`; an unknown name no longer throws |

* `Entity::Component()` and `Container::Component()` take a `std::unique_ptr<ecs::Component>` by value.
  The overloads taking a raw pointer and a `std::shared_ptr` are removed, so those forms stop compiling.
  The caller's handle is empty after the call, and a rejected component is released exactly once. The
  error texts and the rule that a second component of a type replaces the first are unchanged.
* `Container::ResourceGet()` is `const` and returns `std::shared_ptr<const ecs::Resource>` without
  copying the bytes; an unknown name gives an empty pointer instead of an exception. A caller that
  modified the returned copy must now add a changed resource with `ResourceAdd()`.
* `Container::ResourceAdd()` moves its argument when it is given a temporary or a `std::move()`d value.

No data member or virtual function was added to or removed from a public class.

### 1.6.0

New `Container::Stop()`. No existing signature changed and `System` is unchanged. `Container` gained
private members at its end, so code that embeds a `Container` by value must be rebuilt; no plugin
needs rebuilding. The behaviours below changed. See "Start-up, shutdown and stopping" for the rules.

* `System::Shutdown()` now runs. Before, it was never called. A system that overrides it will start
  running that code. It is called once for each started system, on these paths and threads:

  | Path | Thread |
  |---|---|
  | `SystemDestroy()` or replacement | The world thread, when the system is released |
  | `Container::Stop()` or `Manager::Shutdown()` on a world with its own thread | That world's thread, before it ends |
  | Destruction of a world with its own thread, or of its manager | That world's thread, before it ends |
  | `Container::Stop()` on a world driven by `Update()` | The thread that calls `Stop()` |
  | Destruction of a world driven by `Update()`, or of its manager | The thread that destroys it |

  Migration: check that every existing `Shutdown()` override is safe to run, that it uses only what is
  still valid then (the world's tables and log destination are), and that it does not block for long.
  Release resources there that were released by process exit before.
* Systems registered after a world started, and systems in a world driven by `Update()`, are now started
  with `Initialize()` before their first update. Before, they were never started unless the code called
  `SystemsInitialize()`. Migration: remove code that relies on `Initialize()` being skipped for such a
  system. Code that calls `SystemsInitialize()` itself keeps working and does not start a system twice.
* `Start()` on a world that is started is a no-op, and a stopped world cannot be restarted. Before, a second
  call started a second thread. Migration: make a new world to run again.
* `Manager::Shutdown()` blocks until the worlds with their own threads have stopped, unless it is called
  from a world thread, where it only requests. Before, it returned at once. Migration: do not call it while
  holding a lock that a system also takes; polling `IsRunning()` is unaffected.
* A world thread that fails, in start-up or in an update, now stops every other world of its manager, because
  it calls `Manager::Shutdown()`. Migration: if the other worlds must outlive a failure, catch the error in
  the system.
* `Update()` after a world has been stopped does nothing, and `Defer()` after a stop is dropped. Migration:
  stop a world only when it is finished.
* The program template and `src/example.cpp` call `ECS->Shutdown()` after their loops. Migration: add the call
  to an existing program that relies on process exit to end its world threads, before `main` returns.

No plugin rebuild is required.

### 1.5.0

Public signatures and object layouts are unchanged, so nothing needs to be rebuilt. The behaviours below
changed. See "Input validation" for the rules and the error table.

* A malformed message to `Manager::MessageSubmit()` or `Container::MessageSubmit()` throws
  `std::runtime_error` instead of aborting the process. A field of the wrong type, which used to throw
  `nlohmann::json::type_error`, now throws `std::runtime_error`. Code that catches `type_error` around a
  send should catch `std::runtime_error`.
* An empty world name or system name in a message is rejected. A world created with an empty handle can no
  longer be addressed by message; give it a non-empty handle.
* Registering a system under an identifier that is still in use replaces the earlier system: one system
  remains, at the old position in the update order, started and updated once. Before, the new system was
  also listed a second time, so it was started and updated twice in every pass. Code that registered a second system under a used identifier on purpose should remove the
  first with `SystemDestroy()` if it needs the new one at the end of the order.
* A null system, a system with an empty `Handle` and a system that is already registered are rejected.
  A rejected `System()` call destroys the system that was passed in (an already registered object is not
  deleted).
* A component with an empty `Type`, an empty `EntityHandle` or an unknown entity is rejected. A plugin
  component that never sets `Type`, or a factory that returns null, now fails when it is loaded. Set `Type`
  in the component's constructor.
* A rejected `Entity::Component()` call deletes the component that was passed in. Do not use the pointer
  afterwards.
* A second component of a type on the same entity replaces the first; before, the outcome was not
  defined.
* Creating an `Entity` with a null container throws.

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

The programs are `test_Manager`, `test_System`, `test_Entity`, `test_Container`, `test_Timing` (schedule
arithmetic, with explicit instants), `test_Elapsed` (elapsed time, the first update, stalls and the world
clock, on a controlled clock), `test_Compatibility` (the deprecated names), `test_UpdateAllocation`,
`test_Uuid`, `test_Threading`, `test_Validation`, `test_Lifecycle`, `test_ProcessManager`, `test_Access`,
`test_Logging` (severity, held lines, colour, the library warning, construction-time identifiers),
`test_Resources` and `test_Export`. The script tests are `run-test-selftest.sh`, `run-example.sh`,
`run-logging.sh` (console writes outside the default destination, and output to a file and a terminal),
`check-exports.sh` and `check-exports-selftest.sh` (exported names) and `check-style.sh` with
`check-style-selftest.sh` (`this->` and member naming).

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
