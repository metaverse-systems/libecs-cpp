#include <libecs-cpp/ecs.hpp>
#include <thread>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <iostream>

namespace ecs
{
    Container::Container(ecs::Manager *manager):
        Manager(manager), Handle(ecs::Uuid().Get())
    {
    }

    Container::Container(ecs::Manager *manager, const std::string &handle):
        Manager(manager), Handle(handle)
    {
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
        for(auto &[handle, system] : this->Systems)
        {
            if(this->disabledSystems.contains(handle)) continue;
            try
            {
                system->Initialize();
            }
            catch(const std::exception &e)
            {
                std::cerr << "ecs::Container(\"" << this->Handle
                          << "\")::SystemsInitialize(): System \"" << handle
                          << "\" threw during Initialize(): " << e.what() << std::endl;
                this->disabledSystems.insert(handle);
            }
            catch(...)
            {
                std::cerr << "ecs::Container(\"" << this->Handle
                          << "\")::SystemsInitialize(): System \"" << handle
                          << "\" threw unknown exception during Initialize()" << std::endl;
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
        for(auto &[handle, system] : this->Systems)
        {
            if(this->disabledSystems.contains(handle)) continue;
            try
            {
                if(system->Timing.ShouldUpdate())
                    system->UpdateSystem();
            }
            catch(const std::exception &e)
            {
                std::cerr << "ecs::Container(\"" << this->Handle
                          << "\")::Update(): System \"" << handle
                          << "\" threw during Update(): " << e.what() << std::endl;
                this->disabledSystems.insert(handle);
            }
            catch(...)
            {
                std::cerr << "ecs::Container(\"" << this->Handle
                          << "\")::Update(): System \"" << handle
                          << "\" threw unknown exception during Update()" << std::endl;
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
}
