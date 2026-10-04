# libecs-cpp reference

This document states how the library behaves, topic by topic. Read the [guide](GUIDE.md) first if you are new
to the library. The description of each class and member is in the API documentation, which `make doxygen`
builds and which is published at https://metaverse-systems.github.io/libecs-cpp/.

## Contents

* [Terms](#terms)
* [Errors](#errors)
* [Running a container](#running-a-container)
* [Systems](#systems)
* [Timers](#timers)
* [Time](#time)
* [Entities, components and resources](#entities-components-and-resources)
* [Messages](#messages)
* [Deferred changes](#deferred-changes)
* [Logging](#logging)
* [Identifiers](#identifiers)
* [Threading](#threading)

## Terms

| Term | Meaning |
|---|---|
| Container | An `ecs::Container`: the entities, components, systems and resources that are updated together. |
| Handle | The text name of a container, an entity or a system: its `Handle` member. The program chooses it, or the library generates one from an [identifier](#identifiers). |
| Type name | The text a component is stored and looked up under: its `Type` member. |
| Pass | One call of `Container::Update()`. The [deferred functions](#deferred-changes) run, the start-up step runs, and then each system that is due is updated once. |
| Start-up step | The part of a pass that calls `Initialize()` on every system not yet started. `Container::SystemsInitialize()` runs the same step by itself. |
| Started, shut down | A system is started once `Initialize()` has been called on it, and shut down once `Shutdown()` has. |
| Threaded container | A container on which `Start()` was called. It has its own thread, which runs passes at a fixed interval. |
| Application-driven container | A container without a thread. The application calls `Update()` itself. |
| Container thread | The thread that runs a container's passes: the thread of a threaded container, or the thread the application uses to call `Update()`. |
| Container code | Code that the library calls during a pass or a shutdown: system methods, timer callbacks and deferred functions. |
| Application thread | A thread that is not running container code, for example the main thread while it waits. |

## Errors

Message submission, system registration, component attachment and the `Entity` constructors check their
input before they change anything. A rejected call:

* throws `std::runtime_error`, which can be caught;
* leaves the library exactly as it was: no mailbox gains a message and no table gains an entry;
* leaves the container usable.

The error text has the form `<function>: <condition>.` The function is written like
`ecs::Manager::MessageSubmit()` or `ecs::Container("main")::System()`, where `main` is the container's
handle. A value of the wrong JSON type adds `, got <type>`.

A rejection inside a system's `Update()` can be caught there. If it is not caught, it is handled like any
other exception from a system: see [When a system throws](#when-a-system-throws).

The rejected inputs are listed with each operation: [Registering a system](#registering-a-system),
[Attaching a component](#attaching-a-component) and [Message format](#message-format).

## Running a container

A container is run in one of two ways. A threaded container updates itself:

```cpp
ecs::Manager manager;
ecs::Container *container = manager.Container("main");
container->System(std::make_unique<MySystem>());   // MySystem derives from ecs::System
container->Start();        // MySystem::Initialize() runs on the container thread, then updates begin
// ... the program runs ...
manager.Shutdown();        // blocks until MySystem::Shutdown() has run and the thread has ended
```

An application-driven container is updated by the application:

```cpp
ecs::Manager manager;
ecs::Container *container = manager.Container("main");
container->System(std::make_unique<MySystem>());
bool running = true;
while (running)
{
    container->Update();   // starts any system not yet started, then updates
    // ... set running to false when the program should end ...
}
container->Stop();         // MySystem::Shutdown() runs here, on this thread
```

A program that uses the process-wide manager, `ECS`, shuts it down before `main` returns:

```cpp
int main()
{
    ecs::Container *container = ECS->Container("main");
    container->System(std::make_unique<MySystem>());
    container->Start();
    // ... the program runs ...
    ECS->Shutdown();       // stops every threaded container and waits for it
    return 0;
}
```

### Starting a container

* `Start()` starts the container's thread with 30 passes a second. `Start(interval)` sets the time between
  passes; see [Intervals](#intervals) for the accepted range.
* `Start()` takes effect once. A repeated call returns normally and does nothing: no second thread, no
  second start of any system, and the interval stays as it was.
* A container that has been stopped cannot be started again, and a container of a manager that has shut
  down cannot be started at all. Each of those two calls logs one warning and returns. Make a new
  container instead.
* Call `Start()` from the thread that owns the container, before any other thread uses it.

### Starting systems

`Initialize()` is called exactly once per system, on the container thread, before the system's first
`Update()`. `Container::Log()` works from it.

* It does not matter how the system arrived. It may be registered before the container started,
  registered later from container code, registered in an application-driven container, or registered as a
  [replacement](#registering-a-system) for another system.
* Every pass begins with the start-up step, so calling `SystemsInitialize()` is optional. Calling it, once
  or several times, never starts a system twice.
* Systems started in one step are started in registration order.
* A system registered during the step is started by the next step and is not updated in the pass that
  registered it.
* A system removed before the step reached it is never started.
* If `Initialize()` throws, the error is logged at level `error` with the system's handle and rethrown.
  The system counts as started: it is not started again, it is updated, and it receives `Shutdown()`
  when it leaves. The systems after it are started by the next start-up step.

### Stopping a container

`Container::Stop()` is safe from any thread and can be called more than once. Destroying a container stops
it the same way and then releases it.

What `Stop()` does depends on the caller:

| Caller | Threaded container | Application-driven container |
|---|---|---|
| An application thread | Asks the container to stop after the pass in progress. Returns after every `Shutdown()` has run and the thread has ended. | Shuts the systems down on the calling thread, then returns. |
| The container's own code, during a pass | Records the request and returns at once. The stop completes when the pass ends. | Shuts the systems down at once, on the calling thread, including the system that made the call. The rest of the pass skips them. |
| Code of a different container | Records the request and returns at once. | Records the request and returns at once. The owner's later `Stop()`, or destroying the container, shuts the systems down. |

* The wait for a stop does not depend on the update interval. An idle container with a 5 second interval
  stops in milliseconds.
* After a stop, `Update()` does nothing. On a threaded container this holds from the moment the stop is
  requested.
* After a stop, a function handed to `Defer()` is dropped, and a system registered later is never started.
* Destruction is exclusive: no other thread may use the container while its destructor runs.

### Shutting systems down

`Shutdown()` is called exactly once on every system that was started, before the system is destroyed. A
system that was never started never receives it. `Container::Log()` works from `Shutdown()` and from a
system's destructor.

* After `Shutdown()` the system is not updated and receives no message.
* When a container stops or is destroyed, its systems are shut down in the reverse of registration order.
  The container's tables and log destination are still in place while they are.
* An exception thrown from `Shutdown()` is logged at level `error` with the system's handle and swallowed.
  The other systems are still shut down and released, and nothing escapes a destructor.
* A `Shutdown()` that blocks is waited for. The library does not abandon it.

While a container is being stopped, a `Shutdown()` may do the following:

* Remove another system that has not been shut down yet. That system receives `Shutdown()` once and is
  released after the remaining systems have been shut down.
* Register a system. It is not started and is released without `Shutdown()`.
* Call `Container::Stop()` or `Manager::Shutdown()`. Neither waits and neither repeats the shutdown.

### Which thread runs Shutdown()

| How the system leaves | Thread that runs `Shutdown()` | When |
|---|---|---|
| `SystemDestroy()` outside a pass | The container thread (the caller) | During the call, before the system is destroyed |
| `SystemDestroy()` during a pass or a start-up step | The container thread | When the outermost pass or step ends |
| [Replacement](#registering-a-system) | The container thread | When the outermost pass or step ends, or during the call outside one |
| `Container::Stop()` or `Manager::Shutdown()` on a threaded container | The container's own thread | After the pass in progress, before the thread ends |
| Destruction of a threaded container | The container's own thread | The destructor stops the thread first |
| `Container::Stop()` on an application-driven container | The thread that calls `Stop()` | Before `Stop()` returns |
| Destruction of an application-driven container | The thread that destroys it | During the destructor |
| `~Manager()` | Each threaded container's own thread, then the destroying thread for application-driven containers | The destructor shuts down first, then destroys the containers |

### Manager shutdown and destruction

* `Manager::Shutdown()` from an application thread sets `IsRunning()` to `false`, asks every threaded
  container to stop, and returns after each of them has shut its systems down and its thread has ended.
  After it returns, no thread of the manager runs and no system is updated.
* `Manager::Shutdown()` from container code only records the request and returns at once. A system can
  therefore call it from `Update()` without waiting for itself, and two containers can call it at the same
  time. An application thread's later call does the waiting.
* `Manager::Shutdown()` is idempotent, and concurrent callers each return after the work is complete.
* `IsRunning()` never goes back to `true`. Other threads see a request the next time they call it.
* `Manager::Shutdown()` does not stop application-driven containers, because the library does not run
  application code on threads it does not own. Stop them with `Stop()` or destroy them. It does not
  destroy any container.
* When a threaded container's pass or start-up step fails with an exception, its thread logs the error,
  calls `Manager::Shutdown()`, shuts its own systems down and ends. Every threaded container of the manager
  therefore stops. If the other containers must outlive a failure, catch the error in the system.
* `~Manager()` runs `Shutdown()` first, while every container still exists, so a `Shutdown()` that sends a
  message to another container finds it. It then closes the manager: later sends and creation of
  containers fail with `std::runtime_error`. Then the containers are destroyed.

### The process-wide manager

`ECS` is never destroyed, so the library cannot stop its containers at process exit. A program that uses
it calls `ECS->Shutdown()` from its main thread before `main` returns. The library installs no automatic
hook.

## Systems

A system derives from `ecs::System` and overrides `Update()` and `Export()`. It may also override
`Initialize()`, `Shutdown()` and `Configure()`.

```cpp
class Loader : public ecs::System
{
  public:
    Loader() : ecs::System("game/Loader") {}       // the handle
    void Update() override {}                      // called on each pass in which the system is due
    nlohmann::json Export() const override { return {}; }
};
```

### Naming a system

A system's handle is `System::Handle`, a `const std::string` that the base constructor sets.
`ecs::System("name")` uses the name given, and `ecs::System()` generates a UUID. Nothing can change the
handle afterwards, so a subclass names itself in its initialiser list, as `Loader` does above.

### Registering a system

`Container::System(std::make_unique<Loader>())` registers the system and returns a pointer to it. The
system is in `Systems` when the call returns, and its `Container` member is set.

These calls are [rejected](#errors):

| Condition | Key phrase of the error | After the call |
|---|---|---|
| Null pointer | `system is missing` | Unchanged. |
| `Handle` is `""` | `system handle is empty` | Unchanged. The system that was passed in is destroyed. |
| The object is already registered in a container | `system "<handle>" is already registered` | Unchanged. The pointer is released and the object is not deleted, because it belongs to its container. |

Registering a system under the handle of a system that is still registered **replaces** that system:

* The container keeps one system for that handle, in the old one's position in the update order.
* The new instance is started and updated once per pass. It receives messages sent after the call.
* Messages that were waiting for the old instance are discarded with it.
* The old instance is not updated again. It stays in memory until the outermost pass or start-up step
  ends. Raw pointers to it must not be used after the call.

### Update order

* Systems are updated in registration order, each at most once per pass.
* A system is updated in a pass only if its interval has passed; see [Intervals](#intervals).
* Adding or removing systems never changes the relative order of the others.
* A pass in which nothing is added or removed allocates no memory and does not copy the system list.

### Adding and removing systems during a pass

Container code may add and remove systems while a pass or a start-up step is running.

Removing:

* `SystemDestroy()` accepts any handle, including a member of the system being removed, as in
  `SystemDestroy(system->Handle)`. The same holds for `EntityDestroy()` and `ComponentDestroy()`.
* A handle that is not registered, including `""`, is a silent no-op. Removing a system twice releases it
  once.
* A removed system stops being part of the container when the call returns. It is not in `Systems`, is
  not exported, cannot receive messages, and is not started or updated again, even later in the same
  pass. Its timers do not fire. Its handle may be registered again at once.
* A system removed during a pass or a start-up step stays in memory until that pass or step returns. Code
  running inside it may finish, but must not use the system after that.
* A system removed outside a pass is destroyed at once.
* If a timer callback removes its own system, no further timers of that system fire and its `Update()`
  does not run in that pass.
* If a timer callback removes its own system during a direct `System::UpdateSystem()` call made outside a
  pass, the system stays in memory until the end of the next pass or until the container is destroyed.

Adding:

* A system registered during a pass is not updated in that pass. A system registered during a start-up
  step is not started by that step. In both cases it is updated from the next pass, after every system
  registered before it.
* A system registered under the handle of a system removed in the same pass is a new system. It goes to
  the end of the order, is not updated in that pass, and is not affected when the removed system is
  released.

### When a system throws

* If `Initialize()`, `Update()` or a timer callback throws, the error is logged at level `error` with the
  system's handle and rethrown to the caller of `Update()` or `SystemsInitialize()`.
* On a threaded container the exception is caught by the container's thread, which shuts the manager
  down; see [Manager shutdown and destruction](#manager-shutdown-and-destruction).
* Before the exception leaves the pass, every change requested in the pass completes. Systems removed
  earlier stay removed and are released once, and systems added earlier stay registered.
* Timer changes are kept as well. Timers added or cancelled before the failure stay added or cancelled,
  every one-shot timer that fired is removed (including the one whose callback threw), and timers not
  yet reached do not fire.
* A caller that catches the exception and calls `Update()` again sees a consistent container.

### Scenarios

* **Removing a system twice in one pass.** A system calls `SystemDestroy()` twice with the same handle in
  one pass, either for another system or for itself. The first call removes it, the second is a no-op,
  and the system is released once when the pass returns.
* **Removing and re-registering a handle in one pass.** A system `a` is removed and a new system is
  registered as `a` in the same pass. The new system is not updated in that pass, goes to the end of the
  order, and is updated from the next pass. Releasing the old one at the end of the pass does not touch
  it.

### Not defined

* `Container::Export()` and the `System::Export()` overrides it calls are a `const` query. They must not
  add or remove systems, entities or components.
* Calling `Update()` or `SystemsInitialize()` from inside a pass of the same container is memory safe, but
  no outcome is defined.

## Timers

A timer calls a function after a length of time. A system owns its timers and adds them with `TimerAdd()`:

```cpp
class Saver : public ecs::System
{
  public:
    Saver() : ecs::System("game/Saver")
    {
        // repeating: fires every two hours
        this->TimerAdd(ecs::Timer("autosave", [this] { this->Save(); }, std::chrono::hours(2)));
        // one-shot: fires once, ten seconds from now
        this->TimerAdd(ecs::Timer("welcome", [this] { this->Log("ready"); }, std::chrono::seconds(10), false));
    }
    void Save() {}
    nlohmann::json Export() const override { return {}; }
};
```

* A timer first fires one full length after it was added.
* A repeating timer fires every length until `TimerClear(name)` cancels it. A one-shot timer is removed
  after it fires.
* Timers are checked when their system is updated, before the system's `Update()`. A timer therefore
  cannot fire more often than its system is updated.
* A timer of length zero fires once on every update of its system, and never more than once per update.
* The length is a `std::chrono` duration from zero to `ecs::MAX_INTERVAL`. `TimerAdd()` rejects a length
  outside that range with `std::runtime_error` and adds nothing.
* `TimerClear(name)` cancels every timer with that name.

### Changing timers from a callback

* A timer callback may call `TimerAdd()` on its own system or on any other system of the same container.
* A timer that a callback adds to its own system does not fire in the same update. It is considered from
  the system's next update.
* A timer callback may call `TimerClear(name)` on its own system, including for its own name. A cancelled
  timer that has not been reached yet in this update does not fire.
* After the timers of an update have been checked, exactly the one-shot timers that fired are removed. A
  one-shot timer that adds a new timer with its own name keeps the new timer.
* Cancelling and then adding the same name in one callback leaves exactly the new timer. Adding and then
  cancelling removes both.
* Timer changes made by a callback take effect before the system's own `Update()` runs in the same pass.
* `TimerAdd()` and `TimerClear()` called from `Update()` take effect at once.

Two scenarios:

* **One-shot re-arm under the same name.** A one-shot timer named `retry` fires and calls `TimerAdd()`
  with a new timer named `retry`. The fired timer is removed and the new one is kept, so `retry` fires
  again later.
* **A callback that changes timers and then throws.** A callback calls `TimerAdd()` for a timer `x`,
  `TimerClear()` for a timer `y`, and then throws. The exception reaches the caller of `Update()`. `x` is
  kept, `y` is gone, the callback's own one-shot timer is removed, and the timers after it did not fire.

## Time

### Intervals

An interval is the time between two updates of a repeating schedule. Three things have one: a system
(`System::Timing`), a threaded container (`Start(interval)`) and a repeating timer. Every interval is a
`std::chrono` duration, for example `std::chrono::milliseconds(100)`. A larger interval means fewer
updates.

* **Default.** A system's interval is `ecs::DEFAULT_INTERVAL`, 33 333 microseconds, about 30 updates a
  second. Change it with `this->Timing.SetInterval(std::chrono::milliseconds(100));`.
* **Zero** means every pass: a system with an interval of zero is updated on every pass of its container.
* **Maximum.** The longest interval is `ecs::MAX_INTERVAL`, 100 years of 365.25 days.
* **Range check.** A negative interval or one above the maximum is rejected with `std::runtime_error` and
  nothing is changed. `Timing::SetInterval()`, `System::TimerAdd()` and `Container::Start(interval)` all
  check it.
* **First update.** A new schedule is first due one full interval after it was created, or after
  `Restart(now)`. It is not due before that.
* **Catching up.** When a schedule is checked late, it moves forward by exactly the whole intervals that
  passed, so its average rate stays exact.
* **After a stall.** If more than two whole intervals passed, the schedule is due once and starts counting
  again from that moment. It does not catch up with a burst of updates.

### Elapsed time

`System::ElapsedGet()` returns how much time passed between the previous update of the system and the
current one, in microseconds. `ElapsedSecondsGet()` returns the same value as a `double` in seconds.

```cpp
void Update() override
{
    // Cap the step so that one long stall is not integrated in one go.
    double dt = std::min(this->ElapsedSecondsGet(), 0.25);
    // position += velocity * dt;
}
```

* **Measured once per update.** The clock is read once at the start of each update. That one reading
  decides whether the system is due and what its elapsed time is.
* **Stable within an update.** `ElapsedGet()` returns the same value however many times it is called in
  that update, from `Update()`, a timer callback or helper code.
* **Reading changes nothing.** A system that never reads the elapsed time loses no time.
* **Exact in total.** The values of consecutive updates add up to the time the clock moved, to within the
  one microsecond that truncating the clock reading costs.
* **First update.** There is no earlier update to measure from, so before and during the first update
  `ElapsedGet()` returns the system's interval. A system whose interval is zero gets zero for its first
  update; code that divides by the elapsed time must handle that value or set an interval.
* **No clamp.** After a stall the whole stall is reported: a system that was not updated for five hours
  gets an elapsed time of five hours. Code that integrates over the step should cap it, as above.

### Clocks

Systems and containers read time from an `ecs::Clock`. The default is the real steady clock.

* `System::ClockSet()` replaces the clock of one system. `Container::ClockSet()` replaces the clock of
  every system the container holds and of every system registered later.
* The schedule, timers and elapsed measurement of each affected system start again from the new clock's
  time.
* A clock is not owned by what uses it and must outlive it.
* A null pointer selects the real steady clock.
* The thread of a threaded container waits on the real clock between passes, whatever clock is set. Use
  an `ecs::ManualClock` with application-driven containers.

Testing time without sleeping, with the `Saver` system from [Timers](#timers):

```cpp
ecs::ManualClock clock;
Saver system;
system.ClockSet(&clock);
clock.Advance(std::chrono::hours(2));
system.UpdateSystem();            // the autosave timer fires
```

A schedule can be used by itself, with explicit instants:

```cpp
ecs::Timing schedule(std::chrono::hours(2));
schedule.Restart(std::chrono::microseconds(0));
bool due = schedule.ShouldUpdate(std::chrono::minutes(48));   // false: two hours have not passed
```

### Deprecated names

`Timing::SetFrequency()`, `Timing::GetFrequency()`, `Timing(us)`, a `Timer` length given as a bare number,
`System::DeltaTimeGet()` and the member `System::lastTime` are deprecated since 1.8.0. They compile with a
notice that names the replacement and keep their earlier units. No removal is scheduled; a removal will be
announced in the release notes first. The replacements are in the
[migration table of release 1.8.0](NEWS.md#180).

## Entities, components and resources

An entity is a handle. Its data is in its components, and each component is stored under its entity and
its type name. A component derives from `ecs::Component`, sets `Type` in its constructor and implements
`Export()`:

```cpp
class PositionComponent : public ecs::Component
{
  public:
    float x = 0, y = 0;
    PositionComponent() { this->Type = "PositionComponent"; }
    nlohmann::json Export() const override { return {{"x", this->x}, {"y", this->y}}; }
};
```

A component has no handle of its own. It is addressed by its entity (`EntityHandle`) and its `Type`.

The snippets below use `container`, an `ecs::Container *`, and `entity`, an `ecs::Entity *` made with
`container->Entity()`.

### Attaching a component

```cpp
auto stored = entity->Component(std::make_unique<PositionComponent>());   // stored is a shared_ptr

auto position = std::make_unique<PositionComponent>();
position->EntityHandle = entity->Handle;
container->Component(std::move(position));                                // position is empty afterwards
```

* Both functions take a `std::unique_ptr<ecs::Component>` by value. Raw pointers and `std::shared_ptr`s do
  not compile.
* The container is the sole owner of the component from the moment of the call, also when the call is
  rejected. A rejected component is released exactly once and the caller's pointer is empty.
* `Entity::Component()` sets `EntityHandle`. `Container::Component()` reads it.
* A second component with the same `Type` on the same entity **replaces** the first, so an entity has
  exactly one component of each type name. Code that holds a `shared_ptr` to the replaced component keeps
  a valid object.
* The same type name on different entities is kept for each.

These inputs are [rejected](#errors):

| Condition | Key phrase of the error | After the call |
|---|---|---|
| Null pointer | `component is missing` | Unchanged. |
| `Type` is `""` | `component type is empty` | Unchanged. No entry is made for the type name. |
| `EntityHandle` is `""` (`Container::Component()` only) | `component entity handle is empty` | Unchanged. |
| `EntityHandle` names no entity in the container (never created, destroyed, or misspelled) | `entity "<handle>" does not exist` | Unchanged. |

A null pointer is reported with the entity's name (`ecs::Entity("handle")::Component()`). The other
rejections are made by the container and carry its name (`ecs::Container("main")::Component()`). Creating
an `Entity` with a null container throws `ecs::Entity: container is missing`.

### Looking a component up

```cpp
const std::string positionType = "PositionComponent";

// by entity handle and type name, as a class; empty when there is none
if (auto position = container->ComponentGet<PositionComponent>(entity->Handle, positionType))
{
    position->x += 1;
}
// without a class: any stored component
std::shared_ptr<ecs::Component> any = container->ComponentGet(entity->Handle, positionType);
// the same calls on the entity, without the entity argument
bool has = entity->ComponentHas(positionType);
```

Inside a system, the container is `this->Container`.

* `ComponentGet<T>(entity, type)` returns a `std::shared_ptr<T>`. It is empty when the entity has no
  component with that type name, when the type name has never been used, when the entity is unknown, when
  either string is empty, and when the stored component is not a `T`.
* `ComponentHas(entity, type)` says whether a component is stored under that type name. Only the name is
  compared, so the component's class does not matter.
* Neither call throws, changes the container or takes a lock.
* The result keeps its object valid if the component is later replaced or removed.
* Given existing `std::string` arguments, neither call allocates. A string literal of up to 15 characters
  converts without allocating and a longer one allocates, so code that runs every pass should keep its
  type names in `std::string` constants.
* The public table `Components[type][entity]` inserts an empty entry for a type name or entity it does
  not find, as `std::unordered_map::operator[]` does. To read without inserting, use `ComponentGet()`, or
  `find()` or `at()` on the table.

### Resources

A resource is a block of bytes stored under a name.

```cpp
std::vector<uint8_t> bytes = {1, 2, 3};
container->ResourceAdd("tiles", ecs::Resource{std::move(bytes)});   // moved, not copied
if (auto tiles = container->ResourceGet("tiles"))                  // empty when the name is unknown
{
    std::size_t size = tiles->Data.size();                          // read-only, shared
}
```

* `ResourceGet()` returns a `std::shared_ptr<const ecs::Resource>` that shares the stored bytes without a
  copy.
* An unknown name gives an empty pointer. Nothing is created and nothing is thrown.
* The data stays valid for as long as the pointer is held, even after the resource is replaced or the
  container is destroyed.
* Treat a resource as read-only once it has been added.

## Messages

A message is a JSON object that one piece of code sends to a system. Sending:

```cpp
// to a system of any container of the manager
manager.MessageSubmit({
    {"destination", {{"container", "main"}, {"system", "game/Loader"}}},
    {"file", "level1.map"}});

// to a system of this container; destination.container is not read
container->MessageSubmit({{"destination", {{"system", "game/Loader"}}}, {"file", "level1.map"}});
```

Receiving, in the destination system:

```cpp
void Update() override
{
    while (!this->messages.empty())
    {
        nlohmann::json message = this->messages.front();
        this->messages.pop();
        this->Log("asked to load " + message["file"].get<std::string>());
    }
}
```

### Message format

A message is a JSON object with a `destination` object. `destination.system` is non-empty text. For
`Manager::MessageSubmit()`, `destination.container` is non-empty text as well. `Container::MessageSubmit()`
sends to the container that received the call and does not read `destination.container`. Every other
field is delivered unchanged.

These messages are [rejected](#errors). The checks run in the order of the table and stop at the first
failure. A rejected message takes no lock.

| Condition | Key phrase of the error | Manager | Container |
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
`discarded`.

A well-formed message for an unknown destination throws `std::runtime_error` as well:

* `Container <name> not found.` names the container given in the message.
* `System <name> not found.` names the container that was asked for the system. For
  `Container::MessageSubmit()` that is the container that received the call, whatever
  `destination.container` says.

Callers on other threads should catch these errors.

### Message delivery

* A message accepted by `MessageSubmit()` on `Manager`, `Container` or `System` is received by the
  destination system exactly once while that system exists.
* Messages from one sender to one destination arrive in the order sent. No order is promised between
  senders.
* Sending does not wait for a pass and does not call into the destination. A message is never handled
  inside the sender's call.
* A message appears in `messages` at the start of the destination's next update, or earlier if the
  container thread calls `MessagesWaiting()`.
* A message sent during a pass to a system later in the update order is handled in that pass. A message
  to the sender itself, or to a system already updated in the pass, is handled in the next pass.
* A system can be addressed by handle once it has been registered with `Container::System()`.
* If a system is removed while a message is on its way, the message is discarded with the system or the
  send fails as unknown. A message is never delivered to a destroyed object.
* The mailbox is unbounded. The library never drops a message to save memory, so an application that
  needs back-pressure provides it.

Two containers messaging each other:

```cpp
// In a system with the handle "ping" in container "a".
void Update() override
{
    // Received by "pong" in container "b" in its next update.
    this->Container->Manager->MessageSubmit({
        {"destination", {{"container", "b"}, {"system", "pong"}}},
        {"payload", "ping"}});

    // To itself: handled in the next pass.
    this->MessageSubmit({{"destination", {{"container", "a"}, {"system", "ping"}}}});
}
```

## Deferred changes

Only the container thread may change a running container. `Container::Defer(fn)` hands a function to that
thread, which runs it at the start of the next pass:

```cpp
ecs::Manager manager;
ecs::Container *container = manager.Container("main");
container->Start();

// The container is running, so do not call container->Entity() from here.
container->Defer([container]() {
    ecs::Entity *player = container->Entity();
    // ... add components to player ...
});
```

* `Defer()` is safe from any thread, including from container code.
* `fn` runs exactly once, on the container thread, at the start of the next `Update()`, before any system
  is updated and never in the middle of a pass.
* Functions run in the order they were accepted, so one submitter's functions run in submission order.
* Inside `fn`, every operation that is for the container thread only is available.
* A function submitted from inside `fn`, or during a pass, runs in the next pass.
* If `fn` throws, the error is logged at level `error`, the other functions in the batch still run, and
  the first error is rethrown by `Update()` before any system is updated. A threaded container handles it
  as it handles a system's error; see [When a system throws](#when-a-system-throws).
* A container that is never updated never runs its deferred functions.
* Functions pending when the container is stopped or destroyed, or submitted afterwards, are discarded
  without running.
* `fn` and everything it captures must stay valid until it runs or is discarded. A function that needs an
  entity looks it up by handle inside the function.

## Logging

`Container::Log(message, level)` and `System::Log(message, level)` send a line to the container's log
destination. The level defaults to `info`. The usual levels are `error`, `warning`, `info` and `debug`;
any other name is passed on as given. `System::Log()` puts the system's handle in front of the message.

### Default destination

Each container starts with a destination that writes `[level] message`. Lines at `error` and `warning` go
to standard error and every other line goes to standard output.

The tag is coloured only when the stream it is written to is an interactive terminal:

* Output redirected to a file or a pipe carries no escape sequences.
* The environment variable `NO_COLOR`, when set to a non-empty value, turns colour off on a terminal too.
* The two streams are decided separately. With standard output on a terminal and standard error
  redirected, one is coloured and the other is plain.
* The decision is made once, when the container is created. Redirecting a stream later does not change it.
* On Windows a stream counts as a terminal when it is a real console that accepts virtual terminal
  processing.

### Replacing the destination

`LoggerSet(fn)` installs a function that receives exactly `(message, level)` as plain text, with no colour
and no other decoration.

* `LoggerSet()` and `Log()` are safe from any thread at any time.
* Each `Log()` call goes to exactly one destination, one that was installed at some time during the call.
  A call that starts after `LoggerSet()` returned uses the new destination.
* No lock is held while a destination runs, so a destination may call `Log()` or `LoggerSet()` itself.
* An empty function makes later `Log()` calls succeed and print nothing.
* The previous destination may still finish a call that began before the replacement, so it must stay
  callable until then.

### Lines logged before registration

A system may call `Log()` in its constructor, before it belongs to a container. Those lines are held.

* `Container::System()` delivers the held lines to the container's destination, in the order they were
  logged, each with the system's handle as a prefix, before the system is started.
* A registration that is rejected delivers nothing, and the lines stay held.
* A destination that throws does not undo the registration and does not stop the remaining lines.
* Held lines have no cap. A system that logs without limit before it is registered holds them all in
  memory.

### Warnings from the library

The library writes nothing to the console outside the default destination. A warning it raises itself,
for example from `componentsClear()` on a system that is not registered, goes through the system's `Log()`
like any other line. It reaches a replacement destination, and is held when the system has no container
yet.

## Identifiers

An `ecs::Uuid` is a random 128-bit identifier. The library generates one for every handle the program does
not choose. `Get()` returns its text form: 36 lowercase hexadecimal characters with hyphens after the 8th,
12th, 16th and 20th, for example `550e8400-e29b-41d4-a716-446655440000`. Generated identifiers are version
4 with the standard variant.

* **Thread safety.** `ecs::Uuid()` can be called from any number of threads at the same time, including
  threads the library did not create. Each thread has its own generator, seeded independently on that
  thread's first identifier, and no lock is taken.
* **Not secret.** Identifiers are unique in practice but are not cryptographically unpredictable. Do not
  use them as secrets or tokens.
* **Fork.** A child process that forks after generating identifiers continues the parent's sequence, so
  both processes can produce the same identifiers afterwards.
* **Parsing.** `ecs::Uuid(text)` accepts exactly 36 characters in upper or lower case, with hyphens in
  the standard places, and any version or variant, including the all-zero identifier. `Get()` always
  returns lowercase.
* **Rejected text.** The empty string, text of any other length, leading or trailing whitespace, braces,
  a `urn:uuid:` prefix, the form without hyphens, and any character that is not a hexadecimal digit.
* **Errors.** Rejected text throws `std::runtime_error`. The message is a single line of printable ASCII
  that names the problem. For a bad character it gives the 1-based position of the first one found. Long
  input is shortened to its first 64 characters and unprintable bytes are written as `\xNN`.

```
ecs::Uuid(""): text is empty
ecs::Uuid("550e8400-e29b-41d4-a716-44665544000"): text is 35 characters long, expected 36
ecs::Uuid("550e8400xe29b-41d4-a716-446655440000"): expected '-' at position 9, found 'x'
ecs::Uuid("550e8400-e29b-41d4-a716-44665544000g"): invalid character 'g' at position 36, expected a hexadecimal digit
```

## Threading

Every operation belongs to one of three categories:

* **Any thread.** Sending messages, using the manager, requesting and observing shutdown, replacing the
  log destination, logging, and handing the container a deferred function.
* **Container thread only.** Everything that reads or changes entities, components, systems, resources,
  timers or the update order, and everything that reads `System::messages`.
* **Exclusive.** Destructors. No other thread may use the object while one runs.

From another thread, changing the entities, components, systems or resources of a running container
directly is not supported and the outcome is undefined. Use [`Container::Defer()`](#deferred-changes).

A container that is not running (never started, and nobody is calling `Update()`) may be changed directly,
by one thread at a time. Container code runs on the container thread, so it needs no locks to use
`Entities`, `Components`, `Systems` and the other members of its own container.

### Thread category of each member

| Member | Category | Notes |
|---|---|---|
| `Manager::Container(handle)`, `Manager::Container()` | Any thread | One container per handle even when many threads ask at once. The pointer is valid until the manager is destroyed. |
| `Manager::ContainersGet()` | Any thread | A snapshot by value; containers created later are not in it. |
| `Manager::IsRunning()` | Any thread | Atomic. |
| `Manager::Shutdown()` | Any thread | See [Manager shutdown and destruction](#manager-shutdown-and-destruction). |
| `Manager::MessageSubmit(message)` | Any thread | See [Messages](#messages). Also throws while the manager is being destroyed. |
| `Manager::~Manager()` | Exclusive | Shuts down first and waits for sends already in progress. |
| `Container::Start()`, `Start(interval)` | Once | See [Starting a container](#starting-a-container). |
| `Container::Stop()` | Any thread | See [Stopping a container](#stopping-a-container). |
| `Container::Defer(fn)` | Any thread | See [Deferred changes](#deferred-changes). |
| `Container::MessageSubmit(message)` | Any thread | See [Messages](#messages). |
| `Container::Log()`, `Container::LoggerSet()` | Any thread | See [Logging](#logging). |
| `Container::UuidGet()`, `Handle`, `Manager` | Any thread | `Handle` and `Manager` never change after construction. |
| `Container::ClockSet(clock)` | Container thread | |
| `Container::Update()`, `SystemsInitialize()` | Container thread | One thread at a time. |
| `Container::System()`, `SystemDestroy()` | Container thread | |
| `Container::Entity()`, `EntityDestroy()`, `Component()`, `ComponentDestroy()` | Container thread | |
| `Container::ComponentGet()`, `ComponentHas()` | Container thread | Take no lock and change nothing. |
| `Container::ResourceAdd()`, `Resources()`, `ResourceGet()`, `Export()` | Container thread | |
| `Container::Entities`, `Components`, `Systems` | Container thread | Public so systems can iterate them. |
| `Container::~Container()` | Exclusive | Discards pending deferred functions, stops and joins the thread, shuts every started system down. |
| `System::MessageSubmit(message)` | Any thread | The system must be alive. Sending through `Container` or `Manager` is safe against removal; a direct pointer is not. |
| `System::Handle` | Any thread | Never changes after construction. |
| Every other member of `System` | Container thread | |
| `Entity`, `Component`, `Timing` | Container thread | |
| `ecs::Uuid`, `ecs::SteadyClock`, `ecs::ManualClock` | Any thread | |

### Locks and deadlocks

The library has six kinds of mutex:

* the manager's table of containers;
* each container's table of mailboxes;
* each container's queue of deferred functions;
* each container's log destination;
* each container's start and stop state;
* each system's mailbox.

Each is a leaf: a thread holds at most one of them at a time. None is held while user code runs. User
code here means system methods, timer callbacks, log destinations, deferred functions and destructors of
user objects.

The library blocks in two places only:

* `~Manager()` waits for sends that are already in progress. Those sends never block.
* An application thread waits for a container's thread to end in `Stop()`, `Manager::Shutdown()` or a
  destructor.

Container code never waits: a request from there to stop a container or the manager only records the
request. Sending never waits for a container to update. As a result, containers whose systems send
messages to each other in both directions, or to themselves, cannot deadlock.

An application's own locks are its own responsibility. Do not hold one while calling a library function
that your systems also need under that lock.
