# libecs-cpp guide

This guide builds one small program in seven steps. Each step adds one feature of the library: a
container, components, a system, a timer, a message, a deferred change and a clean stop. The
[complete program](#the-complete-program) is at the end. You need the library installed; see the
[README](README.md).

The exact rules behind each step are in the [reference](REFERENCE.md), and each step links to its section.

## The idea

An entity component system splits a program into three kinds of thing:

* An **entity** is one thing in the program, such as a ball. It is only a name, called its handle.
* A **component** is a piece of data attached to an entity, such as a position.
* A **system** is code that runs repeatedly and works on components, such as "move everything that has a
  position and a velocity".

A **container** holds entities, components and systems and updates the systems. A **manager** creates
containers. The library provides one manager for the whole process, `ECS`.

## Step 1: a container and a system

A system derives from `ecs::System`. It passes its handle to the base constructor and overrides `Update()`
and `Export()`. `Initialize()` and `Shutdown()` are optional.

```cpp
class MovementSystem : public ecs::System
{
  public:
    MovementSystem() : ecs::System("MovementSystem") {}

    void Initialize() override { this->Log("started"); }
    void Shutdown() override { this->Log("shut down"); }
    void Update() override {}

    nlohmann::json Export() const override { return {{"Handle", this->Handle}}; }
};
```

`main` asks the manager for a container, registers the system and starts the container:

```cpp
int main()
{
    ecs::Container *container = ECS->Container("main");
    container->System(std::make_unique<MovementSystem>());

    container->Start();
    std::this_thread::sleep_for(std::chrono::seconds(2));

    ECS->Shutdown();
    return 0;
}
```

`Start()` gives the container its own thread. On that thread the library calls `Initialize()` once and
then `Update()` about 30 times a second. `ECS->Shutdown()` stops the thread and calls `Shutdown()` on the
system. A program that uses `ECS` must call it before `main` returns.

Reference: [Running a container](REFERENCE.md#running-a-container), [Systems](REFERENCE.md#systems).

## Step 2: components and an entity

A component derives from `ecs::Component`. It sets `Type`, the name it is stored and looked up under, and
implements `Export()`.

```cpp
class PositionComponent : public ecs::Component
{
  public:
    double x = 0, y = 0;

    PositionComponent(double x, double y) : x(x), y(y)
    {
        this->Type = "PositionComponent";
    }

    nlohmann::json Export() const override
    {
        return {{"x", this->x}, {"y", this->y}};
    }
};
```

`VelocityComponent` is the same with the type name `"VelocityComponent"`. In `main`, before `Start()`,
create an entity and attach one of each:

```cpp
ecs::Entity *ball = container->Entity("ball");
ball->Component(std::make_unique<PositionComponent>(0, 0));
ball->Component(std::make_unique<VelocityComponent>(1, 0.5));
```

The container owns the components from the moment of the call. An entity has at most one component of
each type name; attaching a second one replaces the first.

Reference: [Entities, components and resources](REFERENCE.md#entities-components-and-resources).

## Step 3: a system that works on components

`Update()` moves every entity that has a position and a velocity. A system reaches the components of its
container through two members: `this->Components`, the table of all components by type name and entity
handle, and `this->Container`, the container itself.

```cpp
void Update() override
{
    // Seconds since the previous update, capped so that one long stall is not applied in one go.
    double seconds = std::min(this->ElapsedSecondsGet(), 0.25) * this->speed;

    auto positions = this->Components->find(this->positionType);
    if(positions == this->Components->end()) return;

    for(auto &[entity, component] : positions->second)
    {
        auto position = std::dynamic_pointer_cast<PositionComponent>(component);
        auto velocity = this->Container->ComponentGet<VelocityComponent>(entity, this->velocityType);
        if(!position || !velocity) continue;

        position->x += velocity->x * seconds;
        position->y += velocity->y * seconds;
    }
}
```

Three things to notice:

* `ElapsedSecondsGet()` is the time since this system's previous update. Multiply by it so that the
  result does not depend on how often the system runs.
* `ComponentGet<VelocityComponent>(entity, type)` returns an empty pointer when the entity has no
  velocity, so the loop skips that entity.
* The table is searched with `find()`. Writing `(*this->Components)["PositionComponent"]` would insert an
  empty entry when the type name is missing.

The type names are held in `std::string` members (`positionType`, `velocityType`) so that the lookups do
not build a string on every pass. `speed` is a `double` member that starts at 1; step 5 changes it.

Reference: [Looking a component up](REFERENCE.md#looking-a-component-up), [Elapsed time](REFERENCE.md#elapsed-time).

## Step 4: a timer

A timer calls a function at an interval. The system adds one in its constructor to report the positions
once a second:

```cpp
MovementSystem() : ecs::System("MovementSystem")
{
    this->TimerAdd(ecs::Timer("report", [this] { this->Report(); }, std::chrono::seconds(1)));
}

void Report()
{
    auto positions = this->Components->find(this->positionType);
    if(positions == this->Components->end()) return;

    for(auto &[entity, component] : positions->second)
    {
        this->Log(entity + " is at " + component->Export().dump());
    }
}
```

The timer first fires one second after it was added, and then every second. `this->Log()` writes a line
with the system's handle in front.

Reference: [Timers](REFERENCE.md#timers), [Logging](REFERENCE.md#logging).

## Step 5: a message

Code outside a system talks to it with a message: a JSON object whose `destination` names the container
and the system. `main` asks the system to triple the speed:

```cpp
ECS->MessageSubmit({
    {"destination", {{"container", "main"}, {"system", "MovementSystem"}}},
    {"speed", 3.0}});
```

`MessageSubmit()` is safe from any thread and returns at once. The message appears in the system's
`messages` queue at its next update, and the system reads it at the top of `Update()`:

```cpp
while(!this->messages.empty())
{
    nlohmann::json message = this->messages.front();
    this->messages.pop();

    this->speed = message.value("speed", this->speed);
    this->Log("speed is now " + std::to_string(this->speed));
}
```

Reference: [Messages](REFERENCE.md#messages).

## Step 6: changing a running container

After `Start()`, only the container's own thread may change its entities, components and systems. `main`
is a different thread, so it hands the change to the container with `Defer()`. The function runs on the
container's thread at the start of the next pass.

```cpp
container->Defer([container]() {
    ecs::Entity *rock = container->Entity("rock");
    rock->Component(std::make_unique<PositionComponent>(5, 5));
});
```

The rock has a position and no velocity, so the system reports it and does not move it.

Before `Start()`, `main` could change the container directly, as it did in step 2, because the container
was not running yet.

Reference: [Deferred changes](REFERENCE.md#deferred-changes), [Threading](REFERENCE.md#threading).

## Step 7: stopping

`ECS->Shutdown()` stops every container that has its own thread. It returns after each system's
`Shutdown()` has run and the threads have ended, so nothing of the library is running when `main` returns.

Reference: [Stopping a container](REFERENCE.md#stopping-a-container).

## The complete program

```cpp
#include <libecs-cpp/ecs.hpp>
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

class PositionComponent : public ecs::Component
{
  public:
    double x = 0, y = 0;

    PositionComponent(double x, double y) : x(x), y(y)
    {
        this->Type = "PositionComponent";
    }

    nlohmann::json Export() const override
    {
        return {{"x", this->x}, {"y", this->y}};
    }
};

class VelocityComponent : public ecs::Component
{
  public:
    double x = 0, y = 0;

    VelocityComponent(double x, double y) : x(x), y(y)
    {
        this->Type = "VelocityComponent";
    }

    nlohmann::json Export() const override
    {
        return {{"x", this->x}, {"y", this->y}};
    }
};

class MovementSystem : public ecs::System
{
  public:
    MovementSystem() : ecs::System("MovementSystem")
    {
        this->TimerAdd(ecs::Timer("report", [this] { this->Report(); }, std::chrono::seconds(1)));
    }

    void Initialize() override { this->Log("started"); }
    void Shutdown() override { this->Log("shut down"); }

    void Update() override
    {
        while(!this->messages.empty())
        {
            nlohmann::json message = this->messages.front();
            this->messages.pop();

            this->speed = message.value("speed", this->speed);
            this->Log("speed is now " + std::to_string(this->speed));
        }

        // Seconds since the previous update, capped so that one long stall is not applied in one go.
        double seconds = std::min(this->ElapsedSecondsGet(), 0.25) * this->speed;

        auto positions = this->Components->find(this->positionType);
        if(positions == this->Components->end()) return;

        for(auto &[entity, component] : positions->second)
        {
            auto position = std::dynamic_pointer_cast<PositionComponent>(component);
            auto velocity = this->Container->ComponentGet<VelocityComponent>(entity, this->velocityType);
            if(!position || !velocity) continue;

            position->x += velocity->x * seconds;
            position->y += velocity->y * seconds;
        }
    }

    nlohmann::json Export() const override { return {{"Handle", this->Handle}}; }

  private:
    const std::string positionType = "PositionComponent";
    const std::string velocityType = "VelocityComponent";
    double speed = 1;

    void Report()
    {
        auto positions = this->Components->find(this->positionType);
        if(positions == this->Components->end()) return;

        for(auto &[entity, component] : positions->second)
        {
            this->Log(entity + " is at " + component->Export().dump());
        }
    }
};

int main()
{
    ecs::Container *container = ECS->Container("main");
    container->System(std::make_unique<MovementSystem>());

    ecs::Entity *ball = container->Entity("ball");
    ball->Component(std::make_unique<PositionComponent>(0, 0));
    ball->Component(std::make_unique<VelocityComponent>(1, 0.5));

    container->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    ECS->MessageSubmit({
        {"destination", {{"container", "main"}, {"system", "MovementSystem"}}},
        {"speed", 3.0}});

    container->Defer([container]() {
        ecs::Entity *rock = container->Entity("rock");
        rock->Component(std::make_unique<PositionComponent>(5, 5));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    ECS->Shutdown();
    return 0;
}
```

Build and run it:

```
g++ $(pkg-config --cflags ecs-cpp) guide.cpp $(pkg-config --libs ecs-cpp) -o guide
./guide
```

The output looks like this. The numbers differ a little from run to run:

```
[info] [MovementSystem] started
[info] [MovementSystem] ball is at {"x":0.9685100000000002,"y":0.4842550000000001}
[info] [MovementSystem] ball is at {"x":1.9705289999999993,"y":0.9852644999999997}
[info] [MovementSystem] speed is now 3.000000
[info] [MovementSystem] rock is at {"x":5.0,"y":5.0}
[info] [MovementSystem] ball is at {"x":3.974783,"y":1.9873915}
[info] [MovementSystem] rock is at {"x":5.0,"y":5.0}
[info] [MovementSystem] ball is at {"x":6.981181999999998,"y":3.490590999999999}
[info] [MovementSystem] shut down
```

## Driving the container yourself

`Start()` is optional. A program that has its own loop, or that must do its work on the main thread, calls
`Update()` instead. Each call is one pass: it starts new systems and updates the ones that are due.

```cpp
ecs::Container *container = ECS->Container("main");
container->System(std::make_unique<MovementSystem>());

for(int frame = 0; frame < 100; frame++)
{
    container->Update();
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
}

container->Stop();      // calls Shutdown() on the systems, on this thread
```

Such a container has no thread of its own, so `main` is its thread and may change it directly between
passes. `ECS->Shutdown()` does not stop it; `Stop()` does.

## Where to go next

* [REFERENCE.md](REFERENCE.md) has the rules for every topic above, and for resources, clocks and
  identifiers.
* The API documentation describes each class and member. Build it with `make doxygen` or read it at
  https://metaverse-systems.github.io/libecs-cpp/.
* `src/example.cpp` in the repository is another complete program, which can run either way.
