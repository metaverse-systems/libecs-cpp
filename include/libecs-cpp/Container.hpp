#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <memory>
#include <mutex>
#include <atomic>
#include <iostream>
#include <functional>
#include <libecs-cpp/json.hpp>
#include <libecs-cpp/Resource.hpp>
#include <libecs-cpp/Component.hpp>

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
     * | Start(), Start(interval) | Once: call once, from the thread that owns the world, before any other thread uses it |
     * | Defer(fn) | Any thread |
     * | MessageSubmit(message) | Any thread |
     * | Log(message, level), LoggerSet(fn) | Any thread |
     * | UuidGet(), Handle, Manager | Any thread (Handle and Manager are set at construction and never change) |
     * | Update(), SystemsInitialize() | World thread, one thread at a time |
     * | System(), SystemDestroy() | World thread |
     * | Entity(), EntityDestroy(), Component(), ComponentDestroy() | World thread |
     * | ResourceAdd(), Resources(), ResourceGet(), Export() | World thread |
     * | Entities, Components, Systems | World thread (public so systems can iterate them) |
     * | ~Container() | Exclusive: discards pending deferred changes unrun, stops and joins the thread |
     *
     * Locks: the library uses five mutexes, and each one is a leaf: Manager's container table, this
     * class's mailbox table, deferred queue and log destination, and each system's mailbox. A thread
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
      public:
        Container(ecs::Manager *manager);
        Container(ecs::Manager *manager, const std::string &handle);
        ~Container();
        /*! Starts the world's own thread. Call it once, from the thread that owns the world, before any
         *  other thread uses the world. */
        void Start();
        void Start(uint32_t);
        /*! Calls Initialize() once on every registered system that has not been started yet, in
         *  registration order. Update() does this by itself before the update walk whenever a system
         *  has been registered since the last time, so calling it is optional, and calling it again
         *  never starts a system twice.
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
         * Registering a system under the Handle of a system that is still registered replaces it. The new
         * instance takes the old one's place in the update order, is started and updated once, and
         * receives messages sent after the call. Messages still waiting for the old instance are
         * discarded with it. The old instance stays in memory until the outermost walk ends, but is not
         * visited again. Raw pointers to the replaced system must not be used after the call.
         * System::Container is set by registration only.
         */
        ecs::System *System(std::unique_ptr<ecs::System> system);
        /*! Attaches a component to the entity named by its EntityHandle (world thread only).
         *
         * Four attachments are rejected with std::runtime_error and leave the world unchanged: a null
         * pointer, an empty Type, an empty EntityHandle, and an EntityHandle that names no entity in this
         * world. If the entity already has a component of the same Type, the new component replaces it, so
         * the entity has exactly one component of each type. Anyone holding a shared_ptr to the replaced
         * component keeps a valid object. The same type on a different entity is kept alongside. */
        std::shared_ptr<ecs::Component> Component(std::shared_ptr<ecs::Component> c);
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
         * Called outside a walk, the system is destroyed at once. Called during a walk, or from inside
         * the system's own timer walk, the system stays in memory until the walk returns, so code running
         * inside it may finish, but must not use the system after the walk ends. If the removal happens
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
         * the first exception is rethrown before any system is updated.
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
        /*! World thread only. */
        void ResourceAdd(const std::string &name, ecs::Resource r);
        /*! World thread only. */
        void Resources(const std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> &resources);
        /*! World thread only. */
        ecs::Resource ResourceGet(const std::string &name);
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
         *  so it may itself call Log() or LoggerSet(). If the destination is empty the line is dropped. */
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
        std::vector<std::unique_ptr<ecs::System>> retiredSystems;
        void systemRetire(std::unique_ptr<ecs::System> system);
        /*! Number of microseconds to sleep between Update() calls */
        uint32_t sleepInterval = 1000000 / 30;

        std::jthread containerThread;
        void threadFunc(std::stop_token stopToken);
        ecs::Entity *entityCreate(const std::string &handle);

        std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> resources;

        /*! World thread only. True when a registered system may not have been started yet. */
        bool startPending = false;
        /*! Calls Initialize() on every slot not yet started, in registration order. */
        void systemsStart();
    };
}
