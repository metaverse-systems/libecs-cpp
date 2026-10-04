#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <memory>
#include <type_traits>
#include <mutex>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <libecs-cpp/json.hpp>
#include <libecs-cpp/Uuid.hpp>
#include <libecs-cpp/Resource.hpp>
#include <libecs-cpp/Component.hpp>
#include <libecs-cpp/Clock.hpp>
#include <libecs-cpp/Timing.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace ecs
{
    class Manager;
    class System;
    class Component;
    class Entity;
    struct Mailbox;

    /*! A container: the entities, components, systems and resources that are updated together.
     *
     * A container is driven in one of two ways. After Start() it has its own thread, which runs a pass
     * (Update()) at a fixed interval. Without Start(), the application calls Update() itself. The thread
     * that runs the passes is the container thread. Any other thread is an outside caller.
     *
     * \par Threading
     * Each member states one of three categories on its last line:
     * - Any thread: the member is safe to call from any thread at any time.
     * - Container thread only: the member reads or changes entities, components, systems, resources,
     *   timers or the update order.
     * - Exclusive: no other thread may use the object during the call.
     *
     * \par
     * An outside caller must not change the entities, components, systems or resources of a running
     * container directly. The outcome is undefined. Hand the change to Defer() instead. A container that
     * is not running (never started, and nobody is calling Update()) may be changed directly by one
     * thread at a time. That thread must hand the container over, for example by calling Start(), before
     * another thread uses it. Code inside a container (systems, timer callbacks, deferred functions) runs
     * on the container thread and needs no locks.
     *
     * \par Locks
     * A thread holds at most one of the library's locks at a time, and none is held while user code runs
     * (system methods, timer callbacks, log destinations, deferred functions or destructors of user
     * objects). Sending a message never waits for a container to update. Containers whose systems message
     * each other in both directions, or themselves, therefore cannot deadlock. An application's own locks
     * are its own responsibility.
     *
     * \par Lifetime
     * A pointer returned by Entity() or System() is valid until the object is removed or the container is
     * destroyed. A thread other than the container thread must not hold one across a point where another
     * thread could remove the object. A deferred function that needs an entity looks it up by handle
     * inside the function.
     *
     * The complete rules, with a table of every member, are in the Threading section of REFERENCE.md.
     */
    class Container
    {
        friend class Manager;

      public:
        /*! Creates a container with a generated handle. Applications normally call Manager::Container()
         *  instead, which creates the container and owns it. */
        Container(ecs::Manager *manager);
        /*! Creates a container with the given handle. Applications normally call
         *  Manager::Container(handle) instead, which creates the container and owns it. */
        Container(ecs::Manager *manager, const std::string &handle);
        /*! Stops the container as Stop() does, then releases it. Functions still waiting in Defer() are
         *  discarded without running.
         *
         * Thread: exclusive. */
        ~Container();
        /*! Starts the container's own thread, which runs 30 passes a second.
         *
         * On the thread, systems are started (System::Initialize()) before the first pass, and again
         * whenever a system has been registered since the last pass.
         *
         * Start() takes effect once. A repeated call does nothing: it does not start a second thread, does
         * not start any system a second time and does not change the interval. A container that has been
         * stopped, or whose manager has shut down, cannot be started; the call logs one warning and
         * returns. Make a new container instead.
         *
         * A container that is never started is driven by the application's calls to Update(). The manager
         * does not stop such a container; its owner stops it with Stop() or by destroying it.
         *
         * Thread: call it once, from the thread that owns the container, before any other thread uses the
         * container. */
        void Start();
        /*! Starts the container's own thread with the given time between passes.
         *
         * Behaves as Start(). An interval of zero runs the passes back to back. An interval below zero or
         * above ecs::MAX_INTERVAL throws std::runtime_error and leaves the container not started. The
         * interval does not delay a stop: a stop request ends the wait at once. The thread waits on the
         * real steady clock whatever clock is set with ClockSet(). */
        void Start(std::chrono::microseconds interval);
        /*! Starts the container's own thread with the interval given as a number of microseconds. Behaves
         *  as Start(std::chrono::microseconds). */
        void Start(uint32_t);
        /*! Replaces the clock that the systems of this container read.
         *
         * The new clock is given to every system in the container and to every system registered later.
         * Each system starts its schedule, timers and elapsed-time measurement again from the new clock
         * (see System::ClockSet()). The pointer is not owned and the clock must outlive the container. A
         * null pointer selects the real steady clock, the default. Meant for containers driven by calls to
         * Update(), for example with an ecs::ManualClock in a test.
         *
         * Thread: container thread only. */
        void ClockSet(const ecs::Clock *clock);
        /*! Stops the container and calls System::Shutdown() on every started system, last registered first.
         *
         * Each started system receives Shutdown() once; a system that was never started does not receive
         * it. Calling Stop() again does nothing. A stopped container cannot be restarted. After the call,
         * Update() does nothing and a system registered later is never started.
         *
         * When the call returns depends on the caller and on how the container is driven:
         * - From an application thread, on a container with its own thread: after the pass in progress
         *   has ended, every Shutdown() has run and the thread has ended. The wait does not depend on the
         *   update interval; an idle container stops in milliseconds. Concurrent callers all return after
         *   the stop is complete.
         * - From the container's own thread (a system, a timer callback or a deferred function), or from
         *   the thread of another container: at once. The call only records the request. A container with
         *   its own thread completes the stop when the pass ends; a container driven by Update() is shut
         *   down by its owner's later Stop() or destruction.
         * - On a container driven by Update(), from the thread that drives it: the systems are shut down
         *   on that thread before the call returns. This also holds inside one of the container's own
         *   passes: the systems are shut down at once, including the one that made the call, and the rest
         *   of the pass skips them.
         *
         * During the shutdown:
         * - An error thrown by Shutdown() is logged at level "error" with the system's handle and
         *   swallowed. The remaining systems still receive Shutdown().
         * - Functions still waiting in Defer() are discarded without running.
         * - A system removed from inside a Shutdown() receives its own Shutdown() once and is released
         *   after the remaining systems have been shut down.
         * - A system registered from inside a Shutdown() is never started and is released without
         *   Shutdown().
         * - Stop() called from inside a Shutdown() returns at once.
         *
         * Thread: any. */
        void Stop();
        /*! Calls System::Initialize() on every registered system that has not been started, in registration
         *  order.
         *
         * Update() does this by itself at the start of each pass, so calling it is optional, and calling
         * it again never starts a system twice. It does nothing once the container has been stopped.
         *
         * A system registered during the call is not started by it; it is started and updated from the
         * next Update(). A system removed during the call before it was reached is not started. Systems
         * removed during the call are released when it returns. If Initialize() throws, the error is
         * logged with the system's handle and rethrown after every change requested so far has completed.
         *
         * Thread: container thread only. */
        void SystemsInitialize();
        /*! Registers a system and returns a pointer to it.
         *
         * The system is in Systems when the call returns. It is started (System::Initialize()) once, on
         * the container thread, at the start of the next pass and so before its first update. A system
         * registered after the container has been stopped is never started and is released without
         * Shutdown().
         *
         * \par During a pass
         * A system registered during a pass or during SystemsInitialize() is not started or updated in it.
         * It is updated from the next pass, after every system registered before it. Registering under
         * the handle of a system removed earlier in the same pass creates a new system that goes to the end
         * of the order and is not affected when the removed one is released.
         *
         * \par Replacement
         * Registering under the Handle of a system that is still registered replaces it. The new instance
         * takes the old one's place in the update order, is started and updated once, and receives
         * messages sent after the call. The old instance receives Shutdown() if it was started, and
         * messages still waiting for it are discarded with it. It is not updated again and stays in
         * memory until the outermost pass or SystemsInitialize() call ends. Raw pointers to it must not be
         * used after the call.
         *
         * \par Rejected calls
         * Three calls throw std::runtime_error and leave the container unchanged: a null pointer, a system
         * whose Handle is empty, and a system object that is already registered in a container. A
         * rejected call destroys the system that was passed in, except for an already registered object:
         * that one belongs to its container, so the pointer is released and the object is not deleted.
         *
         * \par Lines logged before registration
         * Lines the system logged before this call are delivered to the log destination as the last step
         * of a successful call, once and in order, each with the system's handle as a prefix, before the
         * system is started. A rejected call delivers nothing. A destination that throws does not stop the
         * remaining lines or undo the registration. Held lines are not capped.
         *
         * Thread: container thread only. */
        ecs::System *System(std::unique_ptr<ecs::System> system);
        /*! Attaches a component to the entity named by its EntityHandle and returns the stored component.
         *
         * The container becomes the sole owner at the call: the caller's pointer is empty afterwards,
         * whether the call is accepted or rejected, and a rejected component is released exactly once.
         * Pass std::make_unique<T>(...) or a std::move()d std::unique_ptr; raw pointers and shared_ptrs do
         * not compile.
         *
         * If the entity already has a component of the same Type, the new component replaces it, so an
         * entity has exactly one component of each type. Anyone holding a shared_ptr to the replaced
         * component keeps a valid object. The same type on a different entity is kept alongside.
         *
         * Throws std::runtime_error, leaving the container unchanged, for a null pointer, an empty Type, an
         * empty EntityHandle, and an EntityHandle that names no entity in this container.
         *
         * Thread: container thread only. */
        std::shared_ptr<ecs::Component> Component(std::unique_ptr<ecs::Component> component);
        /*! Looks up the component with the given type name on the given entity, as class T.
         *
         * The result is empty when the entity has no component with that type name, when the type name
         * has never been used, when the entity is unknown, when either string is empty, and when the
         * stored component is not a T. With the default T, ecs::Component, any stored component is
         * returned. The result keeps its object valid even if the component is later replaced or removed.
         *
         * The call never throws, never changes the container and takes no lock. Unlike
         * Components[type][entity], which inserts an empty entry for a type or entity it does not find,
         * it leaves the table as it was. Given existing std::string arguments it does not allocate. A
         * string literal longer than 15 characters allocates when it is converted to std::string, so code
         * that runs every pass should hold its type names in std::string constants.
         *
         * Thread: container thread only. */
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
        /*! Says whether the given entity has a component stored under the given type name.
         *
         * Only the name is compared, so the component's class does not matter. An empty slot left by
         * Components[type][entity] counts as no component. The call never throws, never changes the
         * container, takes no lock and does not allocate given existing std::string arguments.
         *
         * Thread: container thread only. */
        bool ComponentHas(const std::string &entity, const std::string &type) const;
        /*! Removes the component with the given type name from the given entity. The arguments may be
         *  fields of the component being removed. An unknown entity or type name is a silent no-op.
         *
         * Thread: container thread only. */
        void ComponentDestroy(const std::string &entity, const std::string &type);
        /*! Returns the entity with this handle, creating it if it does not exist. The pointer is valid
         *  until the entity is removed or the container is destroyed.
         *
         * Thread: container thread only. */
        ecs::Entity *Entity(const std::string &handle);
        /*! Creates an entity with a generated handle. The pointer is valid until the entity is removed or
         *  the container is destroyed.
         *
         * Thread: container thread only. */
        ecs::Entity *Entity();
        /*! Removes an entity and its components. The handle may be the entity's own Handle. An unknown
         *  handle, including an empty one, is a silent no-op.
         *
         * Thread: container thread only. */
        void EntityDestroy(const std::string &handle);
        /*! Removes a system.
         *
         * When the call returns the system is no longer part of the container: it is not in Systems, is
         * not exported, cannot receive messages, and is not started or updated again, and its timers do
         * not fire, even later in the same pass. Its handle may be registered again at once. The handle
         * passed in may be the system's own Handle. An unknown handle, including an empty one, is a silent
         * no-op, so removing a system twice releases it once.
         *
         * If the system was started it receives Shutdown() once, just before it is released. An error
         * thrown by Shutdown() is logged and swallowed. When that happens depends on where the call is
         * made:
         * - Outside a pass: during this call, and the system is destroyed at once.
         * - During a pass or during SystemsInitialize(), including from one of the system's own timer
         *   callbacks: when the pass or the SystemsInitialize() call returns. Code running inside the
         *   system may finish, but must not use the system after that.
         * - From one of the system's own timer callbacks during a direct System::UpdateSystem() call made
         *   outside a pass: at the end of the next pass, or when the container is destroyed.
         *
         * Thread: container thread only. */
        void SystemDestroy(const std::string &handle);
        /*! Describes the container, its entities and its systems as JSON. This is a const query: overrides
         *  of System::Export() must not add or remove systems, entities or components.
         *
         * Thread: container thread only. */
        nlohmann::json Export() const;
        /*! Runs one pass: the deferred functions, then the start of new systems, then one update of each
         *  system.
         *
         * - The functions handed to Defer() run first, unless this call is made from inside a pass or from
         *   inside a deferred function. If one of them throws, the rest still run and the first exception
         *   is rethrown before any system is updated.
         * - Initialize() is then called on every system not yet started, as SystemsInitialize() does.
         * - System::UpdateSystem() is then called on each system in registration order, each at most once.
         *
         * After the container has been stopped or destroyed, Update() does nothing. On a container with
         * its own thread it does nothing from the moment a stop is requested.
         *
         * \par Changes during a pass
         * Adding or removing systems never changes the relative order of the others. A system removed
         * earlier in the pass is skipped. A system added during the pass is first updated in the next one.
         * Systems removed during the pass are released when it returns. A pass in which nothing is added
         * or removed allocates no memory and does not copy the system list.
         *
         * \par Errors
         * If a system throws, the error is logged at level "error" with the system's handle and rethrown
         * to the caller. Before it leaves, every change requested in the pass completes: removed systems
         * are released exactly once, added systems stay registered, and the timer changes made before the
         * failure are kept. A caller that catches the exception and calls Update() again finds a
         * consistent container.
         *
         * \par
         * Calling Update() or SystemsInitialize() from inside a pass of the same container is memory safe,
         * but its outcome is not defined.
         *
         * Thread: container thread only, one thread at a time. */
        void Update();
        /*! Sends a message to the system named in message["destination"]["system"].
         *
         * The message must be a JSON object with a "destination" object that holds a non-empty text
         * "system". message["destination"]["container"] is not read: the message goes to this container
         * whatever that field holds. A system can be addressed by handle once it has been registered with
         * System().
         *
         * Throws std::runtime_error, changing nothing and taking no lock, if the message breaks these
         * rules; the text names the missing or wrong field and, for a wrong type, the type found. Also
         * throws std::runtime_error if the system is unknown; the text names this container.
         *
         * Thread: any. */
        void MessageSubmit(const nlohmann::json &message);
        /*! Hands a function to the container thread, which runs it at the start of the next pass.
         *
         * The function runs once, before any system is updated and never in the middle of a pass.
         * Functions run in the order the calls were accepted. A function submitted from a deferred
         * function, or during a pass, runs in the next pass. A container that is never updated never runs
         * them. Functions still pending when the container is stopped or destroyed are discarded without
         * running, and a call made after that is dropped.
         *
         * If a function throws, the error is logged at level "error", the rest of the batch still runs,
         * and the first exception is rethrown by Update() before any system is updated. On the
         * container's own thread it is caught at the thread boundary, as for a system's error.
         *
         * The function and everything it captures must stay valid until it runs or is discarded.
         *
         * Thread: any, including from inside the container. */
        void Defer(std::function<void()> fn);
        /*! Stores a resource under a name, replacing any resource of that name.
         *
         * Passing a temporary or a std::move()d value costs no copy of the bytes; passing an lvalue copies
         * them once. Readers that still hold the replaced resource keep their data. Treat a resource as
         * read-only once added.
         *
         * Thread: container thread only. */
        void ResourceAdd(const std::string &name, ecs::Resource r);
        /*! Stores every resource in the given map under its name, replacing resources of the same names.
         *  The stored resources share their bytes with the map's; nothing is copied.
         *
         * Thread: container thread only. */
        void Resources(const std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> &resources);
        /*! Returns the resource stored under a name as a shared read-only view, without copying its bytes.
         *
         * The result is empty when the name is unknown; nothing is created and nothing is thrown. The
         * data stays valid for as long as the result is held, even after the resource is replaced or the
         * container is destroyed.
         *
         * Thread: container thread only. */
        std::shared_ptr<const ecs::Resource> ResourceGet(const std::string &name) const;
        /*! The entities of the container, by handle. Public so systems can iterate it; create and remove
         *  entities with Entity() and EntityDestroy().
         *
         * Thread: container thread only. */
        std::unordered_map<std::string, std::unique_ptr<ecs::Entity>> Entities;
        /*! The manager that owns the container. Set at construction and never changes.
         *
         * Thread: any. */
        ecs::Manager *Manager = nullptr;
        /*! The container's handle: the name given to Manager::Container(), or a generated UUID. Set at
         *  construction and never changes.
         *
         * Thread: any. */
        const std::string Handle;
        /*! The components of the container, by type name and then by entity handle. Public so systems can
         *  iterate it. Reading Components[type][entity] inserts an empty entry when the type or entity is
         *  missing; ComponentGet() does not.
         *
         * Thread: container thread only. */
        ecs::TypeEntityComponentList Components;
        /*! Generates a new identifier.
         *
         * Thread: any. */
        ecs::Uuid UuidGet();
        /*! The systems of the container, by handle. Public so systems can iterate it; register and remove
         *  systems with System() and SystemDestroy().
         *
         * Thread: container thread only. */
        std::unordered_map<std::string, std::unique_ptr<ecs::System>> Systems;
        /*! Sends a line to the log destination.
         *
         * Each call goes to exactly one destination, one that was installed at some time during the call;
         * a call that starts after LoggerSet() returned uses the new destination. The destination is
         * called with no lock held, so it may itself call Log() or LoggerSet(). If the destination is
         * empty the line is dropped. Lines a system logged before it was registered reach the destination
         * when it is registered.
         *
         * The default destination of a new container writes "[level] message": error and warning to
         * standard error, every other level to standard output. A stream gets colour codes only when it
         * is an interactive terminal and the NO_COLOR environment variable is unset or empty. Each stream
         * is decided once, when the container is created, so redirecting it later does not change the
         * decision. A destination set with LoggerSet() always receives the plain message and level.
         *
         * Thread: any. */
        void Log(const std::string &message, const std::string &level = "info");
        /*! Replaces the log destination.
         *
         * An empty function makes later Log() calls drop their lines. The previous destination may still
         * finish a call that began before the replacement, so it must stay callable until then.
         *
         * Thread: any, including from inside a destination. */
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
        std::vector<SystemSlot> systemOrder;
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
        /*! The clock given to every system. Container thread only. */
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

        /*! Container thread only. True when a registered system may not have been started yet. */
        bool startPending = false;
        /*! Calls Initialize() on every slot not yet started, in registration order. */
        void systemsStart();
        /*! Container thread, or the thread that stops or destroys a container without one. True while teardown
         *  runs and after it has finished. */
        bool tearingDown = false;
        bool tornDown = false;
        /*! Sends Shutdown() to every started system, last registered first, then releases what was removed. */
        void teardown();

        /*! Guards threadStarted, joining, joined, stopDone and the thread object's hand-over. A leaf lock:
         *  nothing else is taken and no user code runs while it is held. */
        std::mutex lifecycleLock;
        std::condition_variable_any lifecycleChanged;
        /*! The container's own thread has been created. Under lifecycleLock. */
        bool threadStarted = false;
        /*! Set under lifecycleLock, readable without it. */
        std::atomic<bool> stopRequested{false};
        /*! The container's thread has finished teardown. Under lifecycleLock. */
        bool stopDone = false;
        /*! One caller has taken the thread to join it. Under lifecycleLock. */
        bool joining = false;
        /*! The container's thread has ended. Under lifecycleLock. */
        bool joined = false;
        /*! True once the container's own thread exists; set before the thread starts and never cleared. */
        std::atomic<bool> ownsThread{false};
        /*! Starts the thread once, if no stop was requested and the manager is running. */
        void threadStart(const std::chrono::microseconds *interval);
        /*! Marks the stop as requested and wakes the container's thread. Takes no lock while calling out. */
        void requestStop();
        /*! Waits until the container's thread has ended (one caller joins, the others wait), or tears down a
         *  container that has no thread on the calling thread. */
        void waitStopped();
        /*! For the manager. Requests the stop of a container that has its own thread and says whether it has
         *  one; a container without a thread is left alone. */
        bool managerStopRequest();
        /*! For the manager. Waits until the container's thread has ended, unless called from a container thread,
         *  which only requests. */
        void managerStopWait();
    };
}
