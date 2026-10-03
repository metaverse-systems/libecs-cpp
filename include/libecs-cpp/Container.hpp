#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <memory>
#include <type_traits>
#include <mutex>
#include <atomic>
#include <iostream>
#include <functional>
#include <condition_variable>
#include <libecs-cpp/json.hpp>
#include <libecs-cpp/Resource.hpp>
#include <libecs-cpp/Component.hpp>
#include <libecs-cpp/Clock.hpp>
#include <libecs-cpp/Timing.hpp>
#include <chrono>
#include <cstdint>

namespace ecs
{
    class Manager;
    class System;
    class Component;
    class Entity;
    struct Mailbox;

    /*! A world: the entities, components, systems and resources that are updated together.
     *
     * Threading
     *
     * The world thread is the thread that runs the world's update passes: the world's own background
     * thread once Start() has been called, or the single thread an application uses to call Update().
     * Any other thread is an outside caller.
     *
     * - Safe from any thread: delivering messages, using the manager, requesting and observing
     *   shutdown, replacing the log destination, logging, and handing the world a deferred change.
     * - World thread only: everything that reads or changes entities, components, systems, resources,
     *   timers or the update order, and everything that reads System::messages.
     * - Unsupported from outside callers of a running world: direct changes to entities, components,
     *   systems or resources. The outcome is undefined. Use Defer() instead.
     * - A world that is not running (never started, and nobody is calling Update()) may be changed
     *   directly from one thread at a time, as before. That thread must hand the world over, for
     *   example by calling Start(), before another thread uses it.
     * - Code inside a world has no new obligations: no locks and no new calls. Entities, Components,
     *   Systems and the update order stay usable from the world thread exactly as before.
     *
     * Operations:
     *
     * | Member | Class |
     * |---|---|
     * | Start(), Start(interval) | Once: call from the thread that owns the world, before any other thread uses it; a repeated call is a no-op |
     * | ClockSet(clock) | World thread |
     * | Defer(fn) | Any thread |
     * | MessageSubmit(message) | Any thread |
     * | Log(message, level), LoggerSet(fn) | Any thread |
     * | UuidGet(), Handle, Manager | Any thread (Handle and Manager are set at construction and never change) |
     * | Update(), SystemsInitialize() | World thread, one thread at a time |
     * | Stop() | Any thread. From the world thread it only requests; elsewhere it returns once the world has been torn down |
     * | System(), SystemDestroy() | World thread |
     * | Entity(), EntityDestroy(), Component(), ComponentDestroy() | World thread |
     * | ComponentGet(), ComponentHas() | World thread; takes no lock |
     * | ResourceAdd(), Resources(), ResourceGet(), Export() | World thread |
     * | Entities, Components, Systems | World thread (public so systems can iterate them) |
     * | ~Container() | Exclusive: discards pending deferred changes unrun, stops and joins the thread, delivers Shutdown() to every started system |
     *
     * Locks: the library uses six mutexes, and each one is a leaf: Manager's container table, and
     * this class's mailbox table, deferred queue, log destination and start/stop state (lifecycleLock),
     * plus one per system mailbox. A thread
     * holds at most one at any moment, and none is held while user code runs (system methods, timer
     * callbacks, message handlers, log destinations, deferred functions or destructors of user
     * objects). Sending a message never waits for a world to update. Worlds whose systems message each
     * other in both directions, or themselves, therefore cannot deadlock. An application's own locks
     * are its own responsibility.
     *
     * Lifetime: pointers returned by Entity(), System() and similar keep their existing lifetime
     * rules. A thread other than the world thread must not hold one across a point where another
     * thread could remove the object. A deferred function that needs an entity looks it up by handle
     * inside the function.
     *
     * Deferred changes: see Defer(). Log destination: see Log() and LoggerSet().
     */
    class Container
    {
        friend class Manager;

      public:
        Container(ecs::Manager *manager);
        Container(ecs::Manager *manager, const std::string &handle);
        ~Container();
        /*! Starts the world's own thread with the default interval (30 passes a second). Call it once,
         *  from the thread that owns the world, before any other thread uses the world.
         *
         * The first call on a world that has never been started, has not been asked to stop, and belongs
         * to a manager that is still running starts the thread. Every other call is a no-op that returns
         * normally: calling Start() again does not start a second thread, does not start any system a
         * second time and does not change the interval. A world that has stopped cannot be started
         * again; make a new world instead. A call that is refused because the world has been stopped or
         * its manager has shut down logs one warning. On the thread, systems are started (System::Initialize())
         * before the first pass, and again whenever a system has been registered since.
         *
         * A world that is never given a thread is driven by the application's calls to Update(). The
         * manager does not stop such a world; its owner stops it with Stop() or by destroying it.
         */
        void Start();
        /*! As Start(), with the time between passes. The interval does not delay a stop: a stop request
         *  ends the wait at once. An interval of zero runs the passes back to back. An interval below zero
         *  or above ecs::MAX_INTERVAL throws std::runtime_error and leaves the world not started.
         *
         * The world's thread waits on the real steady clock whatever clock is set with ClockSet(). */
        void Start(std::chrono::microseconds interval);
        /*! As Start(interval), with the interval as a number of microseconds. */
        void Start(uint32_t);
        /*! Replaces the clock that every system of this world reads, and the clock given to every system
         *  registered later. World thread only. Each system starts its schedule, timers and elapsed-time
         *  measurement again from the new clock (see System::ClockSet()). The pointer is not owned and the
         *  clock must outlive the world. A null pointer selects the real steady clock, the default. Meant
         *  for worlds driven by calls to Update(), for example with an ecs::ManualClock in a test. */
        void ClockSet(const ecs::Clock *clock);
        /*! Stops the world and delivers System::Shutdown() once to every system that was started and has
         *  not been shut down yet, last registered first. Calling it again does nothing. Safe from any
         *  thread.
         *
         * Called from a thread other than the world's, it asks the world to stop after the pass in
         * progress and returns after the world's thread has ended and every notification has been
         * delivered; the wait does not depend on the update interval (an idle world stops within a
         * quarter of a second). Called from the world's own thread (a system, a timer callback or a
         * deferred function) it only asks, returns at once, and the stop completes when the pass ends.
         * Concurrent callers all return after the stop is complete. A stopped world cannot be restarted.
         * A request from the thread of a different world also only asks and returns at once; for a world
         * without a thread that leaves the teardown to its owner's later Stop() or destruction.
         * The destructor does the same stop and is the only call that releases the world; no other
         * thread may use the world while it runs.
         *
         * A world without a thread is torn down on the calling thread before the call returns, also when
         * the call is made from inside one of its own passes: the systems are shut down at once, the
         * rest of that pass skips them, and the system that made the call has already been shut down
         * when the call returns. Errors thrown by Shutdown() are logged at level "error" with the system's identifier and swallowed, and
         * the remaining systems are still notified. Functions still waiting in Defer() are discarded
         * unrun. A system removed from inside a Shutdown() is notified once and released when the
         * teardown walk ends (so after the systems still to be visited); a system
         * registered from inside one is never started and is released without a notification. Calling
         * Stop() from inside a Shutdown() returns at once. After the call, Update() does nothing and a
         * system registered later is never started. */
        void Stop();
        /*! Calls Initialize() once on every registered system that has not been started yet, in
         *  registration order. Update() does this by itself before the update walk whenever a system
         *  has been registered since the last time, so calling it is optional, and calling it again
         *  never starts a system twice. It does nothing once the world has been stopped.
         *
         * This is a walk. A system registered during it does not get Initialize() from this call and is
         * started and updated from the next Update(). A system removed during it is not initialized if it has not
         * been reached yet. Systems removed during the walk are released when it returns, normally or
         * because a system threw. World thread only. If Initialize() throws, the error is logged with the system's
         * identifier and rethrown after every change requested so far has completed.
         */
        void SystemsInitialize();
        /*! Registers a system (world thread only). It is found in Systems as soon as the call returns.
         *
         * Called during a walk, the new system is not updated or initialized in that walk. It is updated
         * from the next pass, after every system registered before it. Registering under the identifier
         * of a system removed earlier in the same walk creates a new system that goes to the end of the
         * order and is not affected when the removed one is released.
         *
         * Three calls are rejected with std::runtime_error and leave the world unchanged: a null pointer,
         * a system whose Handle is empty, and a system object that is already registered in a world. A
         * rejected call destroys the system that was passed in, except for an already registered object:
         * that one belongs to its world, so the pointer is released and the object is not deleted.
         *
         * A system registered at any time is started (System::Initialize()) once, on the world thread,
         * before its first Update(): at the next start-up step, which Update() runs before the update
         * walk. A system registered after the world has been stopped is never started and is released
         * without a notification.
         *
         * Registering a system under the Handle of a system that is still registered replaces it. The new
         * instance takes the old one's place in the update order, is started and updated once, and
         * receives messages sent after the call. The old instance receives Shutdown() if it was started.
         * Messages still waiting for the old instance are discarded with it. The old instance stays in memory until the outermost walk ends, but is not
         * visited again. Raw pointers to the replaced system must not be used after the call.
         * System::Container is set by registration only.
         *
         * Lines the system logged before this call are delivered to the log destination as the last step
         * of a successful call, once and in order, each with the system's identifier as a prefix, before
         * the world starts the system. A rejected call delivers nothing. A destination that throws does
         * not stop the remaining lines or undo the registration. Held lines are not capped.
         */
        ecs::System *System(std::unique_ptr<ecs::System> system);
        /*! Attaches a component to the entity named by its EntityHandle (world thread only).
         *
         * The world becomes the sole owner from the moment of the call: the caller's handle is empty
         * afterwards, whether the call is accepted or rejected, and a rejected component is released
         * exactly once. Pass std::make_unique<T>(...) or std::move(handle); raw pointers, shared_ptrs and
         * copies of a handle do not compile. Returns the stored component.
         *
         * Four attachments are rejected with std::runtime_error and leave the world unchanged: an empty
         * handle, an empty Type, an empty EntityHandle, and an EntityHandle that names no entity in this
         * world. If the entity already has a component of the same Type, the new component replaces it, so
         * the entity has exactly one component of each type. Anyone holding a shared_ptr to the replaced
         * component keeps a valid object. The same type on a different entity is kept alongside. */
        std::shared_ptr<ecs::Component> Component(std::unique_ptr<ecs::Component> component);
        /*! Looks up the component of the given type on the given entity, as kind T (world thread only).
         *
         * The result is empty when the entity has no component of that type, when the type has never been
         * used, when the entity is unknown, when either string is empty, and when the stored component is
         * not a T. With the default kind, ecs::Component, any stored component is returned. The call
         * never throws, never changes the world, takes no lock and, given existing std::string
         * arguments, never allocates. The result keeps its object valid even if the component is later
         * replaced or removed.
         *
         * Unlike Components[type][entity], which inserts an empty entry for a type or entity it does not
         * find, this lookup (like find() and at()) leaves the table as it was. A short string literal
         * (up to 15 characters) does not allocate when converted to std::string, a longer one does, so
         * code that runs every pass should hold its type names in std::string constants. */
        template <class T = ecs::Component>
        std::shared_ptr<T> ComponentGet(const std::string &entity, const std::string &type) const
        {
            const std::shared_ptr<ecs::Component> *found = this->componentFind(entity, type);
            if(found == nullptr)
            {
                return nullptr;
            }
            if constexpr(std::is_same_v<T, ecs::Component>)
            {
                return *found;
            }
            else
            {
                return std::dynamic_pointer_cast<T>(*found);
            }
        }
        /*! True when the given entity has a component stored under the given type name, whatever its kind;
         *  only the name is compared (world thread only). Never throws, never changes the world, takes no
         *  lock and never allocates given existing std::string arguments. An empty slot left by
         *  Components[type][entity] counts as no component. */
        bool ComponentHas(const std::string &entity, const std::string &type) const;
        /*! Removes a component (world thread only). The identifiers may be fields of the component being removed. An
         *  unknown entity or type is a silent no-op. */
        void ComponentDestroy(const std::string &entity, const std::string &type);
        /*! World thread only. */
        ecs::Entity *Entity(const std::string &handle);
        /*! World thread only. */
        ecs::Entity *Entity();
        /*! Removes an entity and its components (world thread only). The identifier may be the entity's own Handle. An
         *  unknown identifier, including an empty one, is a silent no-op. */
        void EntityDestroy(const std::string &handle);
        /*! Removes a system (world thread only).
         *
         * The identifier may be the system's own Handle. An unknown identifier, including an empty one,
         * is a silent no-op, so removing a system twice releases it once. When the call returns the
         * system is no longer part of the world: it is not in Systems, is not exported, cannot receive
         * messages, and is never started, updated or timer-walked again, even later in the same pass. Its
         * identifier may be registered again at once.
         *
         * If the system was started it receives Shutdown() once, just before it is released. Called outside
         * a walk, that happens during this call and the system is destroyed at once. Called during a walk,
         * or from inside the system's own timer walk, the notification is delivered and the system is
         * released when the walk returns, so code running inside it may finish, but must not use the
         * system after the walk ends. An error thrown by Shutdown() is logged and swallowed. If the removal happens
         * in a direct System::UpdateSystem() call made outside a walk, the system stays in memory until
         * the end of the next walk or until the container is destroyed.
         */
        void SystemDestroy(const std::string &handle);
        /*! Describes the world as JSON (world thread only). This is a const query: overrides of System::Export() must not add
         *  or remove systems, entities or components. */
        nlohmann::json Export() const;
        /*! Runs one update pass (world thread only, one thread at a time): calls System::UpdateSystem() on each system in registration order, each at
         *  most once. This is a walk.
         *
         * Before any system is updated, the functions handed to Defer() run, unless this call is made from
         * inside a walk or from inside a deferred function. If one of them throws, the rest still run and
         * the first exception is rethrown before any system is updated. Then the start-up step runs, which
         * calls Initialize() on every system not yet started, and then the update walk. After the world
         * has been stopped or destroyed, Update() does nothing, and on a world with its own thread it
         * returns at once from the moment a stop is requested.
         *
         * Adding or removing systems never changes the relative order of the others. A system removed
         * earlier in the pass is skipped. A system added during the pass is first updated in the next one.
         * A pass in which nothing is added or removed allocates no memory and does not copy the system
         * list. Systems removed during the pass are released when it returns, which is the point where
         * the changes take effect for memory.
         *
         * If a system throws, the error is logged at level "error" with the system's identifier and
         * rethrown to the caller. Before it leaves, every change requested in the pass completes: removed
         * systems are released exactly once, added systems stay registered, and the timer changes made
         * before the failure are kept. A caller that catches the exception and calls Update() again finds
         * a consistent world. Calling Update() or SystemsInitialize() from inside a walk of the same
         * container is memory safe, but its outcome is not defined. All changes to a world must come from
         * the thread that drives its walks.
         */
        void Update();
        /*! Routes a message to the system named in message["destination"]["system"]. Safe to call from
         *  any thread, at any time.
         *
         *  The message must be a JSON object with a "destination" object that holds a non-empty text
         *  "system". The world name in message["destination"]["container"] is not read: a message sent
         *  directly to a world goes to this world whatever that field holds. A message that breaks the
         *  rules throws std::runtime_error naming the missing or wrong field and, for a wrong type, the type
         *  found; nothing is changed and no lock is taken. Throws std::runtime_error if the system is
         *  unknown, and the error names this world, the one that received the call. A system is addressable
         *  by handle once it has been registered with System(). */
        void MessageSubmit(const nlohmann::json &message);
        /*! Queues a change to the world to be made by the world's own thread. Safe to call from any
         *  thread, including from inside the world.
         *
         * The function runs once, on the thread that calls Update(), in the order the calls were accepted,
         * at the start of the next Update(), before any system is updated and never in the middle of a
         * pass. A function submitted from a deferred function, or during a pass, runs in the next pass.
         *
         * If a function throws, the error is logged at level "error", the rest of the batch still runs,
         * and the first exception is rethrown by Update() before any system is updated. On the world's own
         * thread it is caught at the thread boundary, as for a system's error. A world that is never
         * updated never runs them. Functions still pending when the container is destroyed are discarded
         * without running, and a call made after destruction has begun is dropped.
         *
         * The function and everything it captures must stay valid until it runs or is discarded. */
        void Defer(std::function<void()> fn);
        /*! Stores a resource under a name, replacing any resource of that name (world thread only). Passing
         *  a temporary or a std::move()d value costs no copy of the bytes; passing an lvalue copies them
         *  once. Readers that still hold the replaced resource keep their data. Treat a resource as
         *  read-only once added. */
        void ResourceAdd(const std::string &name, ecs::Resource r);
        /*! World thread only. */
        void Resources(const std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> &resources);
        /*! Returns the stored resource as a shared read-only view, without copying its bytes (world thread
         *  only). The result is empty when the name is unknown; nothing is created and nothing is thrown.
         *  The data stays valid for as long as the result is held, even after the resource is replaced or
         *  the world is destroyed. */
        std::shared_ptr<const ecs::Resource> ResourceGet(const std::string &name) const;
        /*! World thread only. Public so systems can iterate it. */
        std::unordered_map<std::string, std::unique_ptr<ecs::Entity>> Entities;
        /*! Any thread. Set at construction and never changes. */
        ecs::Manager *Manager = nullptr;
        /*! Any thread. Set at construction and never changes. */
        const std::string Handle;
        /*! World thread only. Public so systems can iterate it. */
        ecs::TypeEntityComponentList Components;
        /*! Any thread. */
        ecs::Uuid UuidGet();
        /*! World thread only. Public so systems can iterate it; register systems with System(). */
        std::unordered_map<std::string, std::unique_ptr<ecs::System>> Systems;
        /*! Sends a line to the current log destination. Safe to call from any thread. Each call goes to
         *  exactly one destination, one that was installed at some time during the call; a call that starts
         *  after LoggerSet() returned uses the new destination. The destination is called with no lock held,
         *  so it may itself call Log() or LoggerSet(). If the destination is empty the line is dropped.
         *  Lines a system logged before it was registered reach the destination when it is registered. */
        void Log(const std::string &message, const std::string &level = "info");
        /*! Replaces the log destination. Safe to call from any thread, including from inside a destination.
         *  An empty function makes later Log() calls drop their lines. The previous destination may still
         *  finish a call that began before the replacement. */
        void LoggerSet(std::function<void(const std::string &, const std::string &)> fn);
        
      private:
        /*! Maps system handles to their mailboxes so other threads can route without touching Systems. */
        std::mutex mailboxesLock;
        std::unordered_map<std::string, std::shared_ptr<ecs::Mailbox>> mailboxes;
        using LogFunction = std::function<void(const std::string &, const std::string &)>;
        /*! Guards the pointer only; the destination itself is called without the lock. */
        std::mutex loggerLock;
        std::shared_ptr<const LogFunction> logger;
        /*! One position in the update order. A null system marks a system removed during the current walk. */
        struct SystemSlot
        {
            std::string handle;
            ecs::System *system;
            /*! True once Initialize() has been called (or attempted) on this system. */
            bool started = false;
            /*! True once Shutdown() has been called (or attempted) on this system. */
            bool shutdown = false;
        };
        class WalkScope;
        std::vector<SystemSlot> system_order;
        uint32_t walkDepth = 0;
        /*! Guards deferred and deferredClosed only; deferred functions run with no lock held. */
        std::mutex deferredLock;
        std::vector<std::function<void()>> deferred;
        /*! Size of deferred, readable without the lock so an empty queue costs one load per pass. */
        std::atomic<std::size_t> deferredCount{0};
        bool deferredClosed = false;
        bool draining = false;
        void deferredRun();
        bool orderHasGaps = false;
        void walkFinish();
        /*! Systems removed during a walk. They are released when the outermost walk ends. Declared after Systems so they are destroyed first. */
        struct RetiredSystem
        {
            std::unique_ptr<ecs::System> system;
            /*! True when the system was started and has not been sent Shutdown() yet. */
            bool notify = false;
        };
        std::vector<RetiredSystem> retiredSystems;
        /*! Takes the system out of the update order, then sends its notification and releases it, now or
         *  when the outermost walk ends. */
        void systemRetire(std::unique_ptr<ecs::System> system, bool notify);
        /*! Sends Shutdown(), logging and swallowing any error. */
        void systemNotify(ecs::System *system);
        /*! Sends the due notifications of the retired systems and releases them. */
        void retiredRelease();
        void deferredClose();
        /*! Time to sleep between Update() calls */
        std::chrono::microseconds sleepInterval = ecs::DEFAULT_INTERVAL;
        /*! The clock given to every system. World thread only. */
        const ecs::Clock *clock = &ecs::SteadyClock::Instance();

        std::jthread containerThread;
        void threadFunc(std::stop_token stopToken);
        ecs::Entity *entityCreate(const std::string &handle);
        /*! Finds the stored component with find() only, so nothing is inserted. Empty when absent. */
        const std::shared_ptr<ecs::Component> *componentFind(const std::string &entity, const std::string &type) const
        {
            auto byType = this->Components.find(type);
            if(byType == this->Components.end())
            {
                return nullptr;
            }
            auto byEntity = byType->second.find(entity);
            if(byEntity == byType->second.end())
            {
                return nullptr;
            }
            return byEntity->second ? &byEntity->second : nullptr;
        }

        std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> resources;

        /*! World thread only. True when a registered system may not have been started yet. */
        bool startPending = false;
        /*! Calls Initialize() on every slot not yet started, in registration order. */
        void systemsStart();
        /*! World thread, or the thread that stops or destroys a world without one. True while teardown
         *  runs and after it has finished. */
        bool tearingDown = false;
        bool tornDown = false;
        /*! Sends Shutdown() to every started system, last registered first, then releases what was removed. */
        void teardown();

        /*! Guards threadStarted, joining, joined, stopDone and the thread object's hand-over. A leaf lock:
         *  nothing else is taken and no user code runs while it is held. */
        std::mutex lifecycleLock;
        std::condition_variable_any lifecycleChanged;
        /*! The world's own thread has been created. Under lifecycleLock. */
        bool threadStarted = false;
        /*! Set under lifecycleLock, readable without it. */
        std::atomic<bool> stopRequested{false};
        /*! The world's thread has finished teardown. Under lifecycleLock. */
        bool stopDone = false;
        /*! One caller has taken the thread to join it. Under lifecycleLock. */
        bool joining = false;
        /*! The world's thread has ended. Under lifecycleLock. */
        bool joined = false;
        /*! True once the world's own thread exists; set before the thread starts and never cleared. */
        std::atomic<bool> ownsThread{false};
        /*! Starts the thread once, if no stop was requested and the manager is running. */
        void threadStart(const std::chrono::microseconds *interval);
        /*! Marks the stop as requested and wakes the world's thread. Takes no lock while calling out. */
        void requestStop();
        /*! Waits until the world's thread has ended (one caller joins, the others wait), or tears down a
         *  world that has no thread on the calling thread. */
        void waitStopped();
        /*! For the manager. Requests the stop of a world that has its own thread and says whether it has
         *  one; a world without a thread is left alone. */
        bool managerStopRequest();
        /*! For the manager. Waits until the world's thread has ended, unless called from a world thread,
         *  which only requests. */
        void managerStopWait();
    };
}
