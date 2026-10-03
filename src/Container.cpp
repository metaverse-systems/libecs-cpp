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

    Container::~Container()
    {
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
        if(this->containerThread.joinable())
        {
            this->containerThread.request_stop();
            this->containerThread.join();
        }
    }

    void Container::Start()
    {
        this->containerThread = std::jthread([this](std::stop_token st) { this->threadFunc(st); });
    }

    void Container::Start(uint32_t interval)
    {
        this->sleepInterval = interval;
        this->Start();
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
            // Release outside any walk, so a destructor that changes systems sees a settled container.
            std::vector<std::unique_ptr<ecs::System>> released;
            released.swap(this->retiredSystems);
        }
    }

    void Container::systemRetire(std::unique_ptr<ecs::System> system)
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
            this->retiredSystems.push_back(std::move(system));
        }
    }

    ecs::System *Container::System(std::unique_ptr<ecs::System> system)
    {
        system->Container = this;
        system->Components = &(this->Components);
        const std::string handle = system->Handle;
        ecs::System *ptr = system.get();
        {
            std::lock_guard<std::mutex> guard(this->mailboxesLock);
            this->mailboxes[handle] = system->mailbox;
        }

        auto existing = this->Systems.find(handle);
        if(existing != this->Systems.end())
        {
            std::unique_ptr<ecs::System> old = std::move(existing->second);
            for(auto &slot : this->system_order)
            {
                if(slot.system == old.get()) slot.system = ptr;
            }
            existing->second = std::move(system);
            this->system_order.push_back(SystemSlot{handle, ptr});
            this->systemRetire(std::move(old));
            return ptr;
        }

        this->Systems.emplace(handle, std::move(system));
        this->system_order.push_back(SystemSlot{handle, ptr});
        return ptr;
    }

    std::shared_ptr<ecs::Component> Container::Component(std::shared_ptr<ecs::Component> c)
    {
        this->Components[c->Type][c->EntityHandle] = c; 
        return c;
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
        WalkScope walk(this);
        for(size_t i = 0, count = this->system_order.size(); i < count; i++)
        {
            auto *system = this->system_order[i].system;
            if(system == nullptr) continue;
            try
            {
                system->Initialize();
            }
            catch(const std::exception &e)
            {
                this->Log("[" + this->system_order[i].handle + "] threw during Initialize(): " + e.what(), "error");
                throw;
            }
            catch(...)
            {
                this->Log("[" + this->system_order[i].handle + "] threw unknown exception during Initialize()", "error");
                throw;
            }
        }
    }

    void Container::threadFunc(std::stop_token stopToken)
    {
        try
        {
            this->SystemsInitialize();

            while(!stopToken.stop_requested())
            {
                std::this_thread::sleep_for(std::chrono::microseconds(this->sleepInterval));
                this->Update();
            }
        }
        catch(...)
        {
            // Already logged. An exception leaving the thread would call
            // std::terminate, so ask the application to shut down instead.
            if(this->Manager) this->Manager->Shutdown();
        }
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
        if(this->walkDepth == 0 && !this->draining && this->deferredCount.load() != 0)
            this->deferredRun();
        WalkScope walk(this);
        for(size_t i = 0, count = this->system_order.size(); i < count; i++)
        {
            auto *system = this->system_order[i].system;
            if(system == nullptr) continue;
            try
            {
                if(system->Timing.ShouldUpdate())
                    system->UpdateSystem();
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
        this->resources[name] = std::make_shared<ecs::Resource>(r);
    }

    void Container::Resources(const std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> &resources)
    {
        for(auto &[name, resource] : resources)
        {
            this->resources[name] = resource;
        }
    }

    ecs::Resource Container::ResourceGet(const std::string &name)
    {
        if(!this->resources.contains(name))
        {
            auto err = "Attempted to access non-existent resource: " + name;
            throw std::runtime_error(err);
        }

        return *(this->resources[name]);
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

        this->systemRetire(std::move(removed));
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
