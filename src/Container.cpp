#include <libecs-cpp/ecs.hpp>
#include "Validation.hpp"
#include <thread>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <iostream>
#include <exception>

auto loggerFunction = [](const std::string &message, const std::string &level) {
    if(level == "error")
    {
        std::cerr << "\033[91m[" << level << "]\033[0m " << message << std::endl;
        return;
    }
    if(level == "warning")
    {
        std::cerr << "\033[93m[" << level << "]\033[0m " << message << std::endl;
        return;
    }
    if(level == "debug")
    {
        std::cout << "\033[97m[" << level << "]\033[0m " << message << std::endl;
        return;
    }
    std::cout << "\033[92m[" << level << "]\033[0m " << message << std::endl;
};
namespace ecs
{
    namespace
    {
        /*! Non-null on a thread that is running world code (the world's thread, Update(), SystemsInitialize()
         *  or teardown). A stop requested from such a thread never waits. */
        thread_local const Container *worldContext = nullptr;

        class WorldContextScope
        {
          public:
            explicit WorldContextScope(const Container *container): previous(worldContext)
            {
                worldContext = container;
            }
            ~WorldContextScope()
            {
                worldContext = this->previous;
            }
            WorldContextScope(const WorldContextScope &) = delete;
            WorldContextScope &operator=(const WorldContextScope &) = delete;

          private:
            const Container *previous;
        };
    }

    Container::Container(ecs::Manager *manager):
        Manager(manager), Handle(ecs::Uuid().Get())
    {
        this->logger = std::make_shared<const LogFunction>(loggerFunction);
    }

    Container::Container(ecs::Manager *manager, const std::string &handle):
        Manager(manager), Handle(handle)
    {
        this->logger = std::make_shared<const LogFunction>(loggerFunction);
    }

    class Container::WalkScope
    {
      public:
        explicit WalkScope(Container *container): container(container)
        {
            this->container->walkDepth++;
        }
        ~WalkScope()
        {
            this->container->walkFinish();
        }
        WalkScope(const WalkScope &) = delete;
        WalkScope &operator=(const WalkScope &) = delete;
        WalkScope(WalkScope &&) = delete;
        WalkScope &operator=(WalkScope &&) = delete;

      private:
        Container *container;
    };

    void Container::deferredClose()
    {
        std::vector<std::function<void()>> discarded;
        {
            std::lock_guard<std::mutex> guard(this->deferredLock);
            this->deferredClosed = true;
            discarded.swap(this->deferred);
            this->deferredCount.store(0);
        }
        // Destroyed here, after the lock is released, without running.
    }

    Container::~Container()
    {
        this->deferredClose();
        // A threaded world delivers its notifications on its own thread before it ends; a world without
        // one is torn down here.
        this->requestStop();
        this->waitStopped();

        // The logger, the mailbox table and the other members still exist here, so a system may log
        // from its destructor.
        this->retiredRelease();
        std::vector<std::unique_ptr<ecs::System>> owned;
        owned.reserve(this->Systems.size());
        for(auto &slot : this->system_order)
        {
            if(slot.system == nullptr) continue;
            auto found = this->Systems.find(slot.handle);
            if(found != this->Systems.end() && found->second.get() == slot.system)
            {
                owned.push_back(std::move(found->second));
            }
        }
        // Anything left in Systems has no slot; it is released after the others.
        std::vector<std::unique_ptr<ecs::System>> unordered;
        for(auto &[name, system] : this->Systems)
        {
            if(system) unordered.push_back(std::move(system));
        }
        this->Systems.clear();
        this->system_order.clear();
        {
            std::lock_guard<std::mutex> guard(this->mailboxesLock);
            this->mailboxes.clear();
        }
        unordered.clear();
        while(!owned.empty())
        {
            owned.pop_back();
        }
    }

    void Container::requestStop()
    {
        std::stop_source source;
        {
            std::lock_guard<std::mutex> guard(this->lifecycleLock);
            this->stopRequested.store(true);
            if(this->threadStarted) source = this->containerThread.get_stop_source();
        }
        // Stop callbacks run on this thread, so no lock is held here.
        source.request_stop();
        this->lifecycleChanged.notify_all();
    }

    void Container::waitStopped()
    {
        std::unique_lock<std::mutex> lock(this->lifecycleLock);
        if(!this->threadStarted)
        {
            lock.unlock();
            this->teardown();
            return;
        }
        if(this->joined) return;
        if(this->containerThread.joinable() && this->containerThread.get_id() == std::this_thread::get_id())
        {
            // Called on the world's own thread: it cannot wait for itself.
            return;
        }
        if(!this->joining && this->containerThread.joinable())
        {
            this->joining = true;
            std::jthread thread = std::move(this->containerThread);
            lock.unlock();
            thread.join();
            lock.lock();
            this->joined = true;
            this->lifecycleChanged.notify_all();
            return;
        }
        this->lifecycleChanged.wait(lock, [this] { return this->joined; });
    }

    bool Container::managerStopRequest()
    {
        {
            std::lock_guard<std::mutex> guard(this->lifecycleLock);
            if(!this->threadStarted) return false;
        }
        this->requestStop();
        return true;
    }

    void Container::managerStopWait()
    {
        // A world thread never waits: the world it would wait for may be waiting for it.
        if(worldContext != nullptr) return;
        this->waitStopped();
    }

    void Container::Stop()
    {
        this->requestStop();
        if(worldContext != nullptr && worldContext != this)
        {
            // Another world's thread only requests; the application thread does the waiting.
            return;
        }
        if(worldContext == this)
        {
            bool threaded;
            {
                std::lock_guard<std::mutex> guard(this->lifecycleLock);
                threaded = this->threadStarted;
            }
            // On its own thread a threaded world ends its loop and tears down by itself.
            if(threaded) return;
        }
        this->waitStopped();
    }

    void Container::systemNotify(ecs::System *system)
    {
        const std::string handle = system->Handle;
        // The report goes through its own guard: a log destination that throws must not stop the others.
        auto report = [this, &handle](const std::string &what) {
            try
            {
                this->Log("[" + handle + "] threw during Shutdown(): " + what, "error");
            }
            catch(...)
            {
            }
        };
        try
        {
            system->Shutdown();
        }
        catch(const std::exception &e)
        {
            report(e.what());
        }
        catch(...)
        {
            report("unknown exception");
        }
    }

    void Container::teardown()
    {
        if(this->tearingDown || this->tornDown) return;
        this->tearingDown = true;
        WorldContextScope context(this);
        this->deferredClose();
        {
            WalkScope walk(this);
            // Slots added by notifications sit past this count and are not visited.
            for(size_t i = this->system_order.size(); i-- > 0;)
            {
                if(i >= this->system_order.size()) continue;
                auto *system = this->system_order[i].system;
                if(system == nullptr || !this->system_order[i].started || this->system_order[i].shutdown) continue;
                // Marked before the call, so a failing or re-entrant notification is not repeated.
                this->system_order[i].shutdown = true;
                const std::string handle = this->system_order[i].handle;
                this->systemNotify(system);
                std::lock_guard<std::mutex> guard(this->mailboxesLock);
                auto found = this->mailboxes.find(handle);
                if(found != this->mailboxes.end() && found->second == system->mailbox)
                {
                    this->mailboxes.erase(found);
                }
            }
        }
        this->tearingDown = false;
        this->tornDown = true;
    }

    void Container::threadStart(const std::chrono::microseconds *interval)
    {
        const char *refusal = nullptr;
        {
            std::lock_guard<std::mutex> guard(this->lifecycleLock);
            if(this->threadStarted && !this->stopRequested.load())
            {
                // Already running: nothing to do.
                return;
            }
            if(this->stopRequested.load())
            {
                refusal = "Start() ignored: the world has been stopped.";
            }
            else if(this->Manager != nullptr && !this->Manager->IsRunning())
            {
                refusal = "Start() ignored: the manager has shut down.";
            }
            else
            {
                if(interval != nullptr) this->sleepInterval = *interval;
                this->ownsThread.store(true);
                this->threadStarted = true;
                this->containerThread = std::jthread([this](std::stop_token st) { this->threadFunc(st); });
                return;
            }
        }
        this->Log("[" + this->Handle + "] " + refusal, "warning");
    }

    void Container::Start()
    {
        this->threadStart(nullptr);
    }

    void Container::Start(std::chrono::microseconds interval)
    {
        if(interval.count() < 0 || interval > ecs::MAX_INTERVAL)
        {
            throw std::runtime_error("ecs::Container(\"" + this->Handle + "\")::Start(): interval " +
                                     std::to_string(interval.count()) + " us is outside 0 to " +
                                     std::to_string(ecs::MAX_INTERVAL.count()) + " us.");
        }
        this->threadStart(&interval);
    }

    void Container::Start(uint32_t interval)
    {
        this->Start(std::chrono::microseconds(interval));
    }

    void Container::ClockSet(const ecs::Clock *source)
    {
        this->clock = source != nullptr ? source : &ecs::SteadyClock::Instance();
        for(auto &[handle, system] : this->Systems)
        {
            if(system) system->ClockSet(this->clock);
        }
    }

    nlohmann::json Container::Export() const
    {
        nlohmann::json config;

        config["Handle"] = this->Handle;

        for(auto &[name, entity] : this->Entities)
        {
            config["Entities"][name] = entity->Export();
        }

        for(auto &[name, system] : this->Systems)
        {
            config["Systems"][name] = system->Export();
        }

        return config;
    }

    void Container::walkFinish()
    {
        this->walkDepth--;
        if(this->walkDepth == 0 && this->orderHasGaps)
        {
            std::erase_if(this->system_order, [](const SystemSlot &slot) { return slot.system == nullptr; });
            this->orderHasGaps = false;
        }
        if(this->walkDepth == 0 && !this->retiredSystems.empty())
        {
            this->retiredRelease();
        }
    }

    void Container::retiredRelease()
    {
        // Notify and release outside any walk, so user code that changes systems sees a settled container.
        // A notification may retire more systems, so repeat until none are left.
        while(!this->retiredSystems.empty())
        {
            std::vector<RetiredSystem> released;
            released.swap(this->retiredSystems);
            for(auto &retired : released)
            {
                if(retired.notify) this->systemNotify(retired.system.get());
            }
            // Destroyed here, with no walk running.
        }
    }

    void Container::systemRetire(std::unique_ptr<ecs::System> system, bool notify)
    {
        ecs::System *ptr = system.get();
        if(this->walkDepth > 0)
        {
            for(auto &slot : this->system_order)
            {
                if(slot.system == ptr)
                {
                    slot.system = nullptr;
                    this->orderHasGaps = true;
                }
            }
        }
        else
        {
            std::erase_if(this->system_order, [ptr](const SystemSlot &slot) { return slot.system == ptr; });
        }

        if(this->walkDepth > 0 || system->timerWalkDepth > 0)
        {
            // Its own timer walk may still be running, so keep it alive and tell that walk to stop.
            system->removed = true;
            this->retiredSystems.push_back(RetiredSystem{std::move(system), notify});
            return;
        }

        if(notify) this->systemNotify(ptr);
        // system is destroyed here.
    }

    ecs::System *Container::System(std::unique_ptr<ecs::System> system)
    {
        const std::string prefix = "ecs::Container(\"" + this->Handle + "\")::System(): ";
        if(!system)
        {
            throw std::runtime_error(prefix + "system is missing.");
        }
        if(system->Handle.empty())
        {
            throw std::runtime_error(prefix + "system handle is empty.");
        }
        if(system->Container != nullptr)
        {
            // The pointer aliases an object a world already owns, so it must not be deleted here.
            const std::string name = system->Handle;
            system.release();
            throw std::runtime_error(prefix + "system \"" + name + "\" is already registered.");
        }

        system->Container = this;
        system->Components = &(this->Components);
        // Only a different clock restarts the system, so the default setup behaves as before.
        if(system->clock != this->clock) system->ClockSet(this->clock);
        const std::string handle = system->Handle;
        ecs::System *ptr = system.get();

        auto existing = this->Systems.find(handle);
        if(existing != this->Systems.end())
        {
            // Same handle: the new instance takes the old one's place in the update order.
            // Match on the old instance, not the handle: a slot nulled earlier in this walk by a
            // removal keeps the handle but must stay empty.
            std::unique_ptr<ecs::System> old = std::move(existing->second);
            bool notify = false;
            for(auto &slot : this->system_order)
            {
                if(slot.system == old.get())
                {
                    notify = slot.started && !slot.shutdown;
                    slot.system = ptr;
                    slot.started = false;
                    slot.shutdown = false;
                }
            }
            this->startPending = true;
            existing->second = std::move(system);
            {
                std::lock_guard<std::mutex> guard(this->mailboxesLock);
                this->mailboxes[handle] = ptr->mailbox;
            }
            this->systemRetire(std::move(old), notify);
            return ptr;
        }

        this->system_order.push_back(SystemSlot{handle, ptr});
        this->startPending = true;
        try
        {
            this->Systems.emplace(handle, std::move(system));
            std::lock_guard<std::mutex> guard(this->mailboxesLock);
            this->mailboxes[handle] = ptr->mailbox;
        }
        catch(...)
        {
            this->Systems.erase(handle);
            this->system_order.pop_back();
            throw;
        }
        return ptr;
    }

    std::shared_ptr<ecs::Component> Container::Component(std::unique_ptr<ecs::Component> component)
    {
        const std::string prefix = "ecs::Container(\"" + this->Handle + "\")::Component(): ";
        if(!component)
        {
            throw std::runtime_error(prefix + "component is missing.");
        }
        if(component->Type.empty())
        {
            throw std::runtime_error(prefix + "component type is empty.");
        }
        if(component->EntityHandle.empty())
        {
            throw std::runtime_error(prefix + "component entity handle is empty (type \"" + component->Type + "\").");
        }
        if(this->Entities.find(component->EntityHandle) == this->Entities.end())
        {
            throw std::runtime_error(prefix + "entity \"" + component->EntityHandle + "\" does not exist (component type \"" + component->Type + "\").");
        }

        // A component of the same type already on this entity is replaced.
        std::shared_ptr<ecs::Component> stored(std::move(component));
        this->Components[stored->Type][stored->EntityHandle] = stored;
        return stored;
    }

    ecs::Entity *Container::Entity(const std::string &handle)
    {
        return this->entityCreate(handle);
    }

    ecs::Entity *Container::Entity()
    {
        return this->entityCreate(ecs::Uuid().Get());
    }

    ecs::Entity *Container::entityCreate(const std::string &handle)
    {
        if(!this->Entities.contains(handle))
        {
            this->Entities[handle] = std::make_unique<ecs::Entity>(this, handle);
        }

        return this->Entities[handle].get();
    }

    void Container::SystemsInitialize()
    {
        WorldContextScope context(this);
        this->systemsStart();
    }

    void Container::systemsStart()
    {
        if(this->tornDown || this->tearingDown) return;
        // Cleared first: a system registered while this runs sets it again and is started by the next pass.
        this->startPending = false;
        WalkScope walk(this);
        for(size_t i = 0, count = this->system_order.size(); i < count; i++)
        {
            auto *system = this->system_order[i].system;
            if(system == nullptr || this->system_order[i].started) continue;
            // Marked before the call, so a system that fails to start is not started again.
            this->system_order[i].started = true;
            try
            {
                system->Initialize();
            }
            catch(const std::exception &e)
            {
                this->startPending = true;
                this->Log("[" + this->system_order[i].handle + "] threw during Initialize(): " + e.what(), "error");
                throw;
            }
            catch(...)
            {
                this->startPending = true;
                this->Log("[" + this->system_order[i].handle + "] threw unknown exception during Initialize()", "error");
                throw;
            }
        }
    }

    void Container::threadFunc(std::stop_token stopToken)
    {
        WorldContextScope context(this);
        try
        {
            if(!stopToken.stop_requested()) this->SystemsInitialize();

            while(!stopToken.stop_requested())
            {
                {
                    // Ends early, with the lock released, as soon as a stop is requested. A timed wait on some
                    // platforms (for example the Windows thread library) overshoots by about a millisecond, which
                    // would stretch every tick, so the wait ends a little before the deadline and the rest of the
                    // interval is slept.
                    constexpr std::chrono::microseconds slack(2000);
                    const std::chrono::microseconds interval = this->sleepInterval;
                    const auto deadline = std::chrono::steady_clock::now() + interval;
                    if(interval > slack)
                    {
                        std::unique_lock<std::mutex> lock(this->lifecycleLock);
                        this->lifecycleChanged.wait_until(lock, stopToken, deadline - slack,
                                                          [this] { return this->stopRequested.load(); });
                    }
                    if(stopToken.stop_requested() || this->stopRequested.load()) break;
                    std::this_thread::sleep_until(deadline);
                    // A sleep may end a fraction of a millisecond early on some platforms.
                    while(std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
                }
                if(stopToken.stop_requested() || this->stopRequested.load()) break;
                this->Update();
            }
        }
        catch(...)
        {
            // Already logged. An exception leaving the thread would call
            // std::terminate, so ask the application to shut down instead.
            if(this->Manager) this->Manager->Shutdown();
        }
        // Notifications are delivered on this thread before it ends.
        this->teardown();
        {
            std::lock_guard<std::mutex> guard(this->lifecycleLock);
            this->stopDone = true;
        }
        this->lifecycleChanged.notify_all();
    }

    void Container::Defer(std::function<void()> fn)
    {
        {
            std::lock_guard<std::mutex> guard(this->deferredLock);
            if(!this->deferredClosed)
            {
                this->deferred.push_back(std::move(fn));
                this->deferredCount.store(this->deferred.size());
                return;
            }
        }
        // Dropped: fn is destroyed here, outside the lock.
    }

    void Container::deferredRun()
    {
        struct DrainScope
        {
            explicit DrainScope(bool &flag): flag(flag) { this->flag = true; }
            ~DrainScope() { this->flag = false; }
            bool &flag;
        };

        std::vector<std::function<void()>> batch;
        {
            std::lock_guard<std::mutex> guard(this->deferredLock);
            batch.swap(this->deferred);
            this->deferredCount.store(0);
        }

        std::exception_ptr first;
        {
            DrainScope drain(this->draining);
            for(auto &fn : batch)
            {
                try
                {
                    fn();
                }
                catch(const std::exception &e)
                {
                    this->Log("[" + this->Handle + "] a deferred change threw: " + e.what(), "error");
                    if(!first) first = std::current_exception();
                }
                catch(...)
                {
                    this->Log("[" + this->Handle + "] a deferred change threw an unknown exception", "error");
                    if(!first) first = std::current_exception();
                }
            }
        }
        batch.clear();
        if(first) std::rethrow_exception(first);
    }

    void Container::Update()
    {
        if(this->tornDown || this->tearingDown) return;
        if(this->ownsThread.load() && this->stopRequested.load()) return;
        WorldContextScope context(this);
        if(this->walkDepth == 0 && !this->draining && this->deferredCount.load() != 0)
            this->deferredRun();
        if(this->startPending && this->walkDepth == 0)
            this->systemsStart();
        WalkScope walk(this);
        for(size_t i = 0, count = this->system_order.size(); i < count; i++)
        {
            auto *system = this->system_order[i].system;
            // A system shut down earlier in this pass (the world was stopped from inside the pass) is not updated.
            if(system == nullptr || !this->system_order[i].started || this->system_order[i].shutdown) continue;
            try
            {
                const std::chrono::microseconds now = system->clock->Now();
                if(system->Timing.ShouldUpdate(now))
                    system->updateSystem(now);
            }
            catch(const std::exception &e)
            {
                this->Log("[" + this->system_order[i].handle + "] threw during Update(): " + e.what(), "error");
                throw;
            }
            catch(...)
            {
                this->Log("[" + this->system_order[i].handle + "] threw unknown exception during Update()", "error");
                throw;
            }
        }
    }

    void Container::MessageSubmit(const nlohmann::json &message)
    {
        const std::string &dest_system = validation::messageSystem(
            message, validation::Caller{&this->Handle});
        std::shared_ptr<ecs::Mailbox> mailbox;
        {
            std::lock_guard<std::mutex> guard(this->mailboxesLock);
            auto found = this->mailboxes.find(dest_system);
            if(found != this->mailboxes.end()) mailbox = found->second;
        }
        if(!mailbox)
        {
            auto err = "ecs::Container(\"" + this->Handle +
                       "\")::MessageSubmit(): System " + dest_system + " not found.";
            throw std::runtime_error(err);
        }

        std::lock_guard<std::mutex> guard(mailbox->lock);
        mailbox->pending.push_back(message);
        mailbox->count.store(mailbox->pending.size());
    }

    void Container::EntityDestroy(const std::string &handle)
    {
        // Copy first: the caller's string may be a field of the object being removed.
        const std::string target = handle;
        if(!this->Entities.contains(target)) return;

        for(auto &[type, components] : this->Components)
        {
            components.erase(target);
        }
        this->Entities.erase(target);
    }

    void Container::ResourceAdd(const std::string &name, ecs::Resource r)
    {
        this->resources[name] = std::make_shared<ecs::Resource>(std::move(r));
    }

    void Container::Resources(const std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> &resources)
    {
        for(auto &[name, resource] : resources)
        {
            this->resources[name] = resource;
        }
    }

    std::shared_ptr<const ecs::Resource> Container::ResourceGet(const std::string &name) const
    {
        auto it = this->resources.find(name);
        if(it == this->resources.end())
        {
            return nullptr;
        }
        return it->second;
    }

    bool Container::ComponentHas(const std::string &entity, const std::string &type) const
    {
        return this->componentFind(entity, type) != nullptr;
    }

    void Container::ComponentDestroy(const std::string &entity, const std::string &type)
    {
        // Copy first: the caller's strings may be fields of the component being removed.
        const std::string target_entity = entity;
        const std::string target_type = type;
        if(!this->Components.contains(target_type) || !this->Components[target_type].contains(target_entity)) return;
        this->Components[target_type].erase(target_entity);
    }

    void Container::SystemDestroy(const std::string &handle)
    {
        // Copy first: the caller's string may be a field of the system being removed.
        const std::string target = handle;
        auto found = this->Systems.find(target);
        if(found == this->Systems.end()) return;

        std::unique_ptr<ecs::System> removed = std::move(found->second);
        this->Systems.erase(found);
        {
            std::lock_guard<std::mutex> guard(this->mailboxesLock);
            this->mailboxes.erase(target);
        }

        bool notify = false;
        for(const auto &slot : this->system_order)
        {
            if(slot.system == removed.get()) notify = slot.started && !slot.shutdown;
        }
        this->systemRetire(std::move(removed), notify);
    }

    ecs::Uuid Container::UuidGet()
    {
        return ecs::Uuid();
    }

    void Container::Log(const std::string &message, const std::string &level)
    {
        std::shared_ptr<const LogFunction> destination;
        {
            std::lock_guard<std::mutex> lock(this->loggerLock);
            destination = this->logger;
        }
        if(destination && *destination)
        {
            (*destination)(message, level);
        }
    }

    void Container::LoggerSet(std::function<void(const std::string &, const std::string &)> fn)
    {
        auto holder = std::make_shared<const LogFunction>(std::move(fn));
        {
            std::lock_guard<std::mutex> lock(this->loggerLock);
            this->logger.swap(holder);
        }
        // holder now owns the previous destination and releases it here, outside the lock.
    }
}
