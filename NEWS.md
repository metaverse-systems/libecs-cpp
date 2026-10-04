# libecs-cpp release notes

This file holds the versioning rule and the changes of each release, newest first. Each release says who
has to rebuild and how to migrate.

## Versioning

Two numbers describe a release, and they answer different questions.

**The package version** (`2.1.0`, in `configure.ac` and `package.json`) follows semantic versioning for the
source interface: a major number changes when programs have to be changed or rebuilt because the public
headers or the layout of the classes changed, a minor number when something is added or the build and
packaging change without touching the interface, and the last number for fixes. Release 2.1.0 is a minor
release because the source interface and the class layout are the same as in 2.0.0. Consumers need a
rebuild only because the shared library now carries a compatibility version that records what has been true
since 2.0.0.

**The shared-library version** is the libtool triple `current:revision:age` in `src/Makefile.am`
(`-version-info 2:0:0`). It decides the file name: on Linux the soname is `libecs-cpp.so.N` with
`N = current - age`, and on Windows the library is `libecs-cpp-N.dll`. The rule for changing it:

* The soname major starts at the major number of the package, which is why it is `2` and not `0`.
  Earlier releases shipped `0` while the layout of the classes changed and names were removed, so `0` could
  not tell them apart.
* After that, change the triple by the usual libtool rule. Code changes only: raise `revision`. Interface
  added: raise `current`, reset `revision` to 0 and raise `age`. Interface removed or changed
  incompatibly, or the layout of an exported class changed: raise `current`, and reset `revision` and `age`
  to 0. Only the last case changes `N`, so a program keeps running with any newer library of the same `N`
  and the loader refuses a library of a different `N`.
* `tests/check-install.sh` names the expected files, so a change to the triple and to that check go in
  the same commit.

## Release notes

### 2.1.0

Packaging and build release. The source interface and the class layout are the same as in 2.0.0, so this
is a minor release (see [Versioning](#versioning)). What changes is what is installed, how the library is built and
tested, and the name of the shared library.

**Rebuild libthe-seed, the-seed's native addon and every plugin that links libecs-cpp** after installing
2.1.0. They were linked against `libecs-cpp.so.0`; the new library is `libecs-cpp.so.2` (Windows:
`libecs-cpp-2.dll`), so a program built against 2.0.0 is refused by the loader instead of running against
a library it was not built for. Projects keep requiring `ecs-cpp >= 2.0.0`, which 2.1.0 satisfies.

Changed:

* No program is installed. The sample `src/example.cpp` is still built in the build tree and run by the
  tests, but `make install` no longer puts `example` in `bin`. On Windows the only file in `bin` is the DLL.
* `--enable-werror=yes` turns compiler warnings into errors in the library, the sample and the tests. It is
  off by default, so a newer compiler cannot break a build that only installs the library.
* `--enable-tests=auto|yes|no` controls the tests. Catch2 is optional: the default, `auto`, builds the
  tests when Catch2 is found, and `configure` no longer fails without it. `yes` insists on it.
* The shared library is `libecs-cpp.so.2` (with `libecs-cpp.so.2.0.0` and the `libecs-cpp.so` link) and on
  Windows `libecs-cpp-2.dll`. A new install does not remove the files of an older one, so `libecs-cpp.so.0`
  and `libecs-cpp.so.0.0.0` can be left in the library directory; they are harmless and can be deleted
  once nothing needs them. The rule for future changes is in [Versioning](#versioning).
* Every public header can be included on its own, and `Container.hpp` and `System.hpp` no longer include
  `<iostream>`. A program that relied on getting `<iostream>` through the library headers must include it
  itself.
* Out-of-tree builds work, and `make distcheck` passes.
* The installed pkg-config file is checked by building and running a small consumer against an installed
  copy, with `pkg-config` flags only.
* The reference documentation is published as a site (GitHub Pages) by pushes to `master`, and `docs/` is no
  longer stored in the repository. Build it locally with `make doxygen`. The repository owner must switch
  Settings, Pages, Build and deployment, Source to "GitHub Actions" once; until then the publishing job
  reports an error.
* There is one readme, `README.md`; the plain-text `README` is removed, and a check keeps the readme's
  statements about files and options true.
* `npm test` runs `make check` (it was a placeholder that failed).

Migration:

| Before | After |
|---|---|
| A packaging script or a user runs the installed `example` (`PREFIX/bin/example`) | It is not installed. Build the tree and run the sample from the `src` directory of the build, or compile `src/example.cpp` against the installed library. |
| Reading the reference documentation from the committed `docs/` directory | `docs/` is gone. Open the published site, or run `make doxygen` and read `doxygen/html`. |
| Warnings always stopped the build (`-Werror` by default) | Warnings are shown and the build continues. Add `--enable-werror=yes` to get the old behaviour, as the continuous integration does. |
| `configure` failed when Catch2 was missing | The tests are skipped with a message. Use `--enable-tests=yes` to require them, or `--enable-tests=no` to skip them without looking. |
| Scripts that name `libecs-cpp.so.0` or `libecs-cpp.so.0.0.0` | The files are `libecs-cpp.so.2` and `libecs-cpp.so.2.0.0` (Windows: `libecs-cpp-2.dll`). Update the names; delete leftover old files when nothing needs them. |
| libthe-seed and the-seed's native addon built against 2.0.0 | Rebuild and reinstall libthe-seed after libecs-cpp, then rebuild the-seed's addon. Plugins built against 2.0.0 are rebuilt with `the-seed build`. |
| A program that got `<iostream>` through `Container.hpp` or `System.hpp` | Add `#include <iostream>` to that program. |

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

The 2.0.0 release shipped the shared library as `libecs-cpp.so.0`, the same file name as 1.8.0. No libtool
version-info was set for it, so the loader could not tell 1.8.0 and 2.0.0 apart by name. The rebuild rule
above and the `ecs-cpp >= 2.0.0` requirement are what kept the two apart. Release 2.1.0 gives the library a
compatibility version of its own (see [Versioning](#versioning)).

Changed:

* `Component::Handle` is removed. A component has no identifier of its own; address it by its entity and
  type. The two `Component` constructors are defined in the header and the library no longer builds a
  `Component.cpp`.
* `System::Handle` is a `const std::string`. It is set only by the base constructor `System(handle)`
  (or generated by `System()`), and cannot be assigned afterwards.
* `System::Log(message, level)` takes `level = "info"`, the same default as `Container::Log`.
* Lines a system logs before it is registered are delivered to the container's destination at registration,
  in order, before the container starts the system. Before, they waited inside the system until its next
  `Log()` call after registration, and a system that never logged again never delivered them.
* The default destination colours a stream only when it is an interactive terminal, and not when
  `NO_COLOR` is set to a non-empty value. Redirected output is plain text. Before, every line carried
  escape sequences.
* A warning from the library itself (`componentsClear()` with no container) goes through the log destination
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
| `PKG_CHECK_MODULES([LIBECS], [ecs-cpp >= 1.8.0])` | `ecs-cpp >= 2.0.0`. The 2.0.0 release shipped the shared library as `libecs-cpp.so.0`; 2.1.0 ships `libecs-cpp.so.2`. |

Tests: `test_Logging` (default severity, held lines, colour decisions, the library warning and
construction-time identifiers), `test_Resources` and `test_Export` are new, as are the script tests
`run-logging.sh`, `check-exports.sh`, `check-exports-selftest.sh`, `check-style.sh` and
`check-style-selftest.sh`.

### 1.8.0

Clear timer lengths and intervals, trustworthy elapsed time and a clock that tests can control. This is a
minor release although `System` and `Timing` change layout, following the precedent of 1.4.0 to 1.7.0;
the migration of each change is below, and the rules are in [Time](REFERENCE.md#time).

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

Tests: `test_Elapsed` (elapsed time, the first update, stalls and the container clock, all on a controlled
clock) and `test_Compatibility` (every deprecated name still gives its previous result) are new.

Migration:

| Old | New |
|---|---|
| `Timing.SetFrequency(us)` / `GetFrequency()` | `Timing.SetInterval(std::chrono::microseconds(us))` / `GetInterval()` |
| `Timing(us)` | `Timing(std::chrono::microseconds(us))` |
| `Timer("n", cb, 30)` (seconds; a floating-point count no longer compiles) | `Timer("n", cb, std::chrono::seconds(30))` |
| `uint32_t ms = DeltaTimeGet();` | `ElapsedGet()` (microseconds) or `ElapsedSecondsGet()` (seconds, `double`) |
| the protected member `lastTime` | nothing: it is no longer maintained, use `ElapsedGet()` |
| `Start(us)` | unchanged and not deprecated; `Start(std::chrono::microseconds(us))` shows the unit |

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

Component lookups that do not change the container, a sole-ownership attach and shared read-only resources.
This is a minor release although several changes are source-incompatible, following the precedent of 1.4.0
to 1.6.0; the migration of each is below. Plugins that call a changed function must be rebuilt
(`the-seed build`); plugins that only define a component class and `create_component` keep working without
a rebuild, because the changed functions are not part of what they call. This was checked by loading both
kinds of plugin, built against 1.6.0, into 1.7.0: the factory-only plugin loaded and worked, and the plugin
that called `Entity::Component(new ...)` failed with an undefined symbol.

New:

* `Container::ComponentGet<T>(entity, type)` and `ComponentHas(entity, type)`, and the same two on
  `Entity` without the entity argument. See [Entities, components and resources](REFERENCE.md#entities-components-and-resources).
* `src/example.cpp` no longer reads through the table: it looks velocities up with `ComponentGet` and
  skips an entity that has none.

Source-incompatible changes and their migration:

| Old | New |
|---|---|
| `e->Component(new T(cfg))` | `e->Component(std::make_unique<T>(cfg))` |
| `e->Component(loader.Create(...).release())` | `e->Component(loader.Create(...))`, passing the `std::unique_ptr` itself |
| `container->Component(shared)` | `container->Component(std::move(unique))` |
| `(*Components)["T"][entity]` to read a component | `Container->ComponentGet<T>(entity, "T")`, then test the result for empty |
| `auto r = container->ResourceGet(name);` (a copy of the bytes, throws for an unknown name) | `auto r = container->ResourceGet(name);` is a `std::shared_ptr<const ecs::Resource>`: test it for empty and read through `->`; an unknown name no longer throws |

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
needs rebuilding. The behaviours below changed. See [Running a container](REFERENCE.md#running-a-container) for the rules.

* `System::Shutdown()` now runs. Before, it was never called. A system that overrides it will start
  running that code. It is called once for each started system, on these paths and threads:

  | Path | Thread |
  |---|---|
  | `SystemDestroy()` or replacement | The container thread, when the system is released |
  | `Container::Stop()` or `Manager::Shutdown()` on a container with its own thread | That container's thread, before it ends |
  | Destruction of a container with its own thread, or of its manager | That container's thread, before it ends |
  | `Container::Stop()` on a container driven by `Update()` | The thread that calls `Stop()` |
  | Destruction of a container driven by `Update()`, or of its manager | The thread that destroys it |

  Migration: check that every existing `Shutdown()` override is safe to run, that it uses only what is
  still valid then (the container's tables and log destination are), and that it does not block for long.
  Release resources there that were released by process exit before.
* Systems registered after a container started, and systems in a container driven by `Update()`, are now started
  with `Initialize()` before their first update. Before, they were never started unless the code called
  `SystemsInitialize()`. Migration: remove code that relies on `Initialize()` being skipped for such a
  system. Code that calls `SystemsInitialize()` itself keeps working and does not start a system twice.
* `Start()` on a container that is started is a no-op, and a stopped container cannot be restarted. Before, a second
  call started a second thread. Migration: make a new container to run again.
* `Manager::Shutdown()` blocks until the containers with their own threads have stopped, unless it is called
  from a container thread, where it only requests. Before, it returned at once. Migration: do not call it while
  holding a lock that a system also takes; polling `IsRunning()` is unaffected.
* A container thread that fails, in start-up or in an update, now stops every other container of its manager, because
  it calls `Manager::Shutdown()`. Migration: if the other containers must outlive a failure, catch the error in
  the system.
* `Update()` after a container has been stopped does nothing, and `Defer()` after a stop is dropped. Migration:
  stop a container only when it is finished.
* The program template and `src/example.cpp` call `ECS->Shutdown()` after their loops. Migration: add the call
  to an existing program that relies on process exit to end its container threads, before `main` returns.

No plugin rebuild is required.

### 1.5.0

Public signatures and object layouts are unchanged, so nothing needs to be rebuilt. The behaviours below
changed. See [Errors](REFERENCE.md#errors) for the rules; the error tables are with
[systems](REFERENCE.md#registering-a-system), [components](REFERENCE.md#attaching-a-component) and
[messages](REFERENCE.md#message-format).

* A malformed message to `Manager::MessageSubmit()` or `Container::MessageSubmit()` throws
  `std::runtime_error` instead of aborting the process. A field of the wrong type, which used to throw
  `nlohmann::json::type_error`, now throws `std::runtime_error`. Code that catches `type_error` around a
  send should catch `std::runtime_error`.
* An empty container name or system name in a message is rejected. A container created with an empty handle can no
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

* New `Container::Defer()` hands a change to a running container's own thread. See [Deferred changes](REFERENCE.md#deferred-changes).
* The layout of `System`, `Container` and `Manager` changed, so plugins and every consumer built against
  1.3.0 must be rebuilt against the new headers. `the-seed build` does this.
* Systems are addressable by routed messages only if they are registered through `Container::System()`.
  Code that inserts into `Container::Systems` directly must register with
  `Container::System(std::make_unique<...>(...))`.
* `System::MessagesWaiting()` now also counts messages that were delivered but not yet moved to the queue
  (it moves them first), and is for the container thread only. Code that called it from another thread to see a
  backlog should count on the container thread, or have the system publish its own counter.
* The error for a message to an unknown system names the container it was sent to, rather than reading the
  container from the message. The exception type is unchanged.
* A message sent during a pass is first handled at the destination's next update. Do not rely on
  same-call or same-pass handling.
* Changing a running container from outside threads was always a data race. Wrap such a change in
  `Container::Defer()`.
* `~Manager` waits for sends in progress and destroys its containers from its body, which removes a race for
  embedding code that destroys a `Manager` while its container threads message each other.

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

The rules in [Adding and removing systems during a pass](REFERENCE.md#adding-and-removing-systems-during-a-pass)
and [Changing timers from a callback](REFERENCE.md#changing-timers-from-a-callback) are now guaranteed. The object layout of
`ecs::Container`, `ecs::System` and `ecs::Timer` changed (private members only, no public signature
changed), so plugins must be rebuilt against the new headers. `the-seed build` does this.

