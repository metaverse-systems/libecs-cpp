#include <libecs-cpp/ecs.hpp>
#include <thread>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <iostream>

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
        this->logger = loggerFunction;
    }

    Container::Container(ecs::Manager *manager, const std::string &handle):
        Manager(manager), Handle(handle)
    {
        this->logger = loggerFunction;
    }

    Container::~Container()
    {
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
            this->retiredSystems.push_back(std::move(system));
        }
        else
        {
            std::erase_if(this->system_order, [ptr](const SystemSlot &slot) { return slot.system == ptr; });
        }
    }

    ecs::System *Container::System(std::unique_ptr<ecs::System> system)
    {
        system->Container = this;
        system->Components = &(this->Components);
        const std::string handle = system->Handle;
        ecs::System *ptr = system.get();

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

    void Container::Update()
    {
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
        auto dest_system = message["destination"]["system"].get<std::string>();
        if(!this->Systems.contains(dest_system))
        {
            auto err = "ecs::Container(\"" + message["destination"]["container"].get<std::string>() +
                       "\")::MessageSubmit(): System " + dest_system + " not found.";
            throw std::runtime_error(err);
        }

        this->Systems[dest_system]->MessageSubmit(message);
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

        this->systemRetire(std::move(removed));
    }

    ecs::Uuid Container::UuidGet()
    {
        return ecs::Uuid();
    }

    void Container::Log(const std::string &message, const std::string &level)
    {
        if(this->logger)
        {
            this->logger(message, level);
        }
    }

    void Container::LoggerSet(std::function<void(const std::string &, const std::string &)> fn)
    {
        this->logger = std::move(fn);
    }
}
