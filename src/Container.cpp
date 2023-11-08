#include <libecs-cpp/ecs.hpp>
#include <thread>
#include <algorithm>
#include <cstring>

namespace ecs
{
    Container::Container(ecs::Manager *Manager):
        Manager(Manager), Handle(ecs::Uuid().Get())
    {
    }

    Container::Container(ecs::Manager *manager, std::string Handle):
        Manager(manager), Handle(Handle)
    {
    }

    void Container::Start()
    {
        this->ContainerThread = std::thread(&Container::ThreadFunc, this);
        this->ContainerThread.detach();
    }

    void Container::Start(uint32_t interval)
    {
        this->sleep_interval = interval;
        this->Start();
    }

    nlohmann::json Container::Export()
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

    ecs::System *Container::System(ecs::System *system)
    {
        system->Container = this;
        system->Components = &(this->Components);
        this->Systems[system->Handle] = system;
        return system;
    }

    std::vector<std::string> Container::SystemsGet()
    {
        std::vector<std::string> Handles;

        for(auto &[name, system] : this->Systems)
            Handles.push_back(name);

        return Handles;
    }

    std::shared_ptr<ecs::Component> Container::Component(std::shared_ptr<ecs::Component> c)
    {
        this->Components[c->Type][c->EntityHandle] = c; 
        return c;
    }

    ecs::Entity *Container::Entity(std::string Handle)
    {
        return this->EntityCreate(Handle);
    }

    ecs::Entity *Container::Entity()
    {
        return this->EntityCreate(ecs::Uuid().Get());
    }

    ecs::Entity *Container::EntityCreate(std::string Handle)
    {
        if(this->Entities.count(Handle) == 0)
        {
            this->Entities[Handle] = new ecs::Entity(this, Handle);
        }

        return this->Entities[Handle];
    }

    void Container::SystemsInitialize()
    {
        for(auto &Handle : this->SystemsGet())
        {
            this->Systems[Handle]->Initialize();
        }
    }

    void Container::ThreadFunc()
    {
        this->SystemsInitialize();

        while(this->ThreadRunning)
        {
            usleep(this->sleep_interval);	
            this->Update();
        }
    }

    void Container::Update()
    {
        for(auto &Handle : this->SystemsGet())
        {
            if(this->Systems[Handle]->Timing.ShouldUpdate())
                this->Systems[Handle]->Update();
        }
    }

    void Container::MessageSubmit(nlohmann::json message)
    {
        auto dest_system = message["destination"]["system"].get<std::string>();
        if(!this->Systems[dest_system])
        {
            auto err = "ecs::Container(\"" + message["destination"]["container"].get<std::string>() +
                       "\")::MessageSubmit(): System " + dest_system + " not found.";
            throw std::runtime_error(err);
        }

        this->Systems[dest_system]->MessageSubmit(message);
    }

    void Container::EntityDestroy(std::string Handle)
    {
        for(auto &[type, components] : this->Components)
        {
            components.erase(Handle);
        }
        delete this->Entities[Handle];
        this->Entities.erase(Handle);
    }

    void Container::ResourceAdd(std::string name, ecs::Resource r)
    {
        this->resources[name] = r;
    }

    ecs::Resource Container::ResourceGet(std::string name)
    {
        if(this->resources.count(name) == 0)
        {
            auto err = "Attempted to access non-existent resource: " + name;
            throw std::runtime_error(err);
        }

        return this->resources[name];
    }

    void Container::ComponentDestroy(std::string entity, std::string Type)
    {
         this->Components[Type].erase(entity);
    }

    ecs::Uuid Container::UuidGet()
    {
#if USE_BUILTIN_UUID
        return ecs::Uuid(this->uuidGenerator.getUUID().str());
#else
        return ecs::Uuid();
#endif
    }
}
