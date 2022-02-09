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
        this->ContainerThread = std::thread(&Container::ThreadFunc, this);
        this->ContainerThread.detach();
    }

    Json::Value Container::Export()
    {
        Json::Value data;

        data["Handle"] = this->Handle;

        for(auto &[name, entity] : this->Entities)
        {
            data["Entities"][name] = entity->Export();
        }

        for(auto &[name, system] : this->Systems)
        {
            data["Systems"][name] = system->Export();
        }

        return data;
    }

    ecs::System *Container::System(ecs::System *system)
    {
        system->Container = this;
        this->Systems[system->Handle] = system;
        return system;
    }

    std::vector<std::string> Container::SystemsGet()
    {
        std::vector<std::string> Handles;

        for(auto &system : this->Systems)
            Handles.push_back(system.first);

        return Handles;
    }

    std::shared_ptr<ecs::Component> Container::Component(std::shared_ptr<ecs::Component> c)
    {
        this->Components[c->Type][c->EntityHandle] = c; 
        return c;
    }

    ecs::TypeEntityComponentList Container::ComponentsGet()
    {
        return this->Components;
    }

    ecs::TypeEntityComponentList Container::ComponentsGet(std::vector<std::string> Types)
    {
        ecs::TypeEntityComponentList c;

        for(auto &t : Types) c[t] = this->Components[t];
        return c;
    }

    ecs::Entity *Container::Entity(std::string uuid)
    {
        return this->EntityCreate(uuid);
    }

    ecs::Entity *Container::Entity()
    {
        return this->EntityCreate("");
    }

    ecs::Entity *Container::EntityCreate(std::string uuid)
    {
        ecs::Entity *e;
        if(uuid.size() == 0)
        {
            e = new ecs::Entity();
            this->Entities[e->HandleGet()] = e;
        }
        else
        {
            if(this->Entities.count(uuid) == 0)
            {
                e = new ecs::Entity(uuid);
                this->Entities[uuid] = e;
            }
            else
            {
                e = this->Entities[uuid];
            }
        }

        e->ContainerSet(this);
        return e;
    }

    void Container::SystemsInit()
    {
        for(auto &Handle : this->SystemsGet())
        {
            this->Systems[Handle]->Init();
        }
    }

    void Container::ThreadFunc()
    {
        this->SystemsInit();

#ifdef WITHGPERFTOOLS
       ProfilerStart("container_update.log");
#endif
        while(this->ThreadRunning)
        {
            usleep(this->sleep_interval);	
            this->Update();
        }
#ifdef WITHGPERFTOOLS
       ProfilerStop();
#endif
    }

    void Container::Update()
    {
        for(auto &Handle : this->SystemsGet())
        {
            this->Systems[Handle]->Update();
        }
    }

    void Container::MessageSubmit(Json::Value message)
    {
        auto dest_system = message["destination"]["system"].asString();
        if(!this->Systems[dest_system])
        {
            auto err = "ecs::Container(\"" + message["destination"]["container"].asString() + "\")::MessageSubmit(): System " + dest_system + " not found.";
            throw std::runtime_error(err);
        }

        this->Systems[dest_system]->MessageSubmit(message);
    }

    void Container::EntityDestroy(std::string uuid)
    {
        for(auto &t : this->Components)
        {
            t.second.erase(uuid);
        }
        this->Entities.erase(uuid);
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
}
