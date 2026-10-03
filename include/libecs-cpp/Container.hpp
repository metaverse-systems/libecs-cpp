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

    class Container
    {
      public:
        Container(ecs::Manager *manager);
        Container(ecs::Manager *manager, const std::string &handle);
        ~Container();
        void Start();
        void Start(uint32_t);
        /*! Calls Initialize() on every registered system, in registration order.
         *
         * This is a walk. A system registered during it does not get Initialize() from this call and is
         * updated from the next Update(). A system removed during it is not initialized if it has not
         * been reached yet. Systems removed during the walk are released when it returns, normally or
         * because a system threw. If Initialize() throws, the error is logged with the system's
         * identifier and rethrown after every change requested so far has completed.
         */
        void SystemsInitialize();
        /*! Registers a system. It is found in Systems as soon as the call returns.
         *
         * Called during a walk, the new system is not updated or initialized in that walk. It is updated
         * from the next pass, after every system registered before it. Registering under the identifier
         * of a system removed earlier in the same walk creates a new system that goes to the end of the
         * order and is not affected when the removed one is released. The outcome of registering a second
         * system under an identifier that is still in use is unspecified, but memory safe.
         */
        ecs::System *System(std::unique_ptr<ecs::System> system);
        std::shared_ptr<ecs::Component> Component(std::shared_ptr<ecs::Component> c);
        /*! Removes a component. The identifiers may be fields of the component being removed. An
         *  unknown entity or type is a silent no-op. */
        void ComponentDestroy(const std::string &entity, const std::string &type);
        ecs::Entity *Entity(const std::string &handle);
        ecs::Entity *Entity();
        /*! Removes an entity and its components. The identifier may be the entity's own Handle. An
         *  unknown identifier, including an empty one, is a silent no-op. */
        void EntityDestroy(const std::string &handle);
        /*! Removes a system.
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
        /*! Describes the world as JSON. This is a const query: overrides of System::Export() must not add
         *  or remove systems, entities or components. */
        nlohmann::json Export() const;
        /*! Runs one update pass: calls System::UpdateSystem() on each system in registration order, each at
         *  most once. This is a walk.
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
         *  any thread. Throws std::runtime_error if the system is unknown; a system is addressable by
         *  handle once it has been registered with System(). */
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
        void ResourceAdd(const std::string &name, ecs::Resource r);
        void Resources(const std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> &resources);
        ecs::Resource ResourceGet(const std::string &name);
        std::unordered_map<std::string, std::unique_ptr<ecs::Entity>> Entities;
        ecs::Manager *Manager = nullptr;
        const std::string Handle;
        ecs::TypeEntityComponentList Components;
        ecs::Uuid UuidGet();
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
    };
}
