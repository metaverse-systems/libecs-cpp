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

    ecs::System *Container::System(std::unique_ptr<ecs::System> system)
    {
        system->Container = this;
        system->Components = &(this->Components);
        auto handle = system->Handle;
        this->Systems[handle] = std::move(system);
        this->system_order_.push_back(handle);
        return this->Systems[handle].get();
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
        for(const auto &handle : this->system_order_)
        {
            if(this->disabledSystems.contains(handle)) continue;
            auto it = this->Systems.find(handle);
            if(it == this->Systems.end()) continue;
            try
            {
                it->second->Initialize();
            }
            catch(const std::exception &e)
            {
                this->Log("[" + handle + "] threw during Initialize(): " + e.what(), "error");
                this->disabledSystems.insert(handle);
            }
            catch(...)
            {
                this->Log("[" + handle + "] threw unknown exception during Initialize()", "error");
                this->disabledSystems.insert(handle);
            }
        }
    }

    void Container::threadFunc(std::stop_token stopToken)
    {
        this->SystemsInitialize();

        while(!stopToken.stop_requested())
        {
            std::this_thread::sleep_for(std::chrono::microseconds(this->sleepInterval));
            this->Update();
        }
    }

    void Container::Update()
    {
        for(const auto &handle : this->system_order_)
        {
            if(this->disabledSystems.contains(handle)) continue;
            auto it = this->Systems.find(handle);
            if(it == this->Systems.end()) continue;
            try
            {
                if(it->second->Timing.ShouldUpdate())
                    it->second->UpdateSystem();
            }
            catch(const std::exception &e)
            {
                this->Log("[" + handle + "] threw during Update(): " + e.what(), "error");
                this->disabledSystems.insert(handle);
            }
            catch(...)
            {
                this->Log("[" + handle + "] threw unknown exception during Update()", "error");
                this->disabledSystems.insert(handle);
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
        if(!this->Entities.contains(handle)) return;

        for(auto &[type, components] : this->Components)
        {
            components.erase(handle);
        }
        this->Entities.erase(handle);
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
        if(!this->Components.contains(type) || !this->Components[type].contains(entity)) return;
        this->Components[type].erase(entity);
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
