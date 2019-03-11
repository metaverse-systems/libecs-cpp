#include <uuid/uuid.h>
#include <libecs-cpp/ecs.hpp>
#include <thread>
#include <chrono>
#include <unistd.h>

namespace ecs
{
    Container::Container()
    {
        uuid_t uuid;
        uuid_generate(uuid);

        this->Handle.resize(40);
        uuid_unparse(uuid, &this->Handle[0]);

        this->ContainerThread = std::thread(&Container::ThreadFunc, this);
        this->ContainerThread.detach();
    }

    Container::Container(std::string Handle)
    {
        this->Handle = Handle;
        this->ContainerThread = std::thread(&Container::ThreadFunc, this);
        this->ContainerThread.detach();
    }

    Json::Value Container::save()
    {
        Json::Value data;

        data["handle"] = this->Handle;

        for(auto &e : this->Entities)
        {
            ecs::Entity *entity = this->Entities[e.first];

            data["entities"][e.first] = entity->save();
        }

        return data;
    }

    void Container::ManagerSet(ecs::Manager *Manager)
    {
        this->Manager = Manager;
    }

    ecs::Manager *Container::ManagerGet()
    {
        return this->Manager;
    }

    std::string Container::HandleGet()
    {
        return this->Handle;
    }

    ecs::System *Container::System(ecs::System *system)
    {
        system->ContainerSet(this);
        this->Systems[system->HandleGet()] = system;
        return system;
    }

    std::vector<std::string> Container::SystemsGet()
    {
        std::vector<std::string> Handles;

        for(auto &system : this->Systems)
            Handles.push_back(system.first);

        return Handles;
    }

    ecs::Component *Container::Component(ecs::Component *c)
    {
        this->Components[c->Type][c->Handle] = c; 
        return c;
    }

    std::map<std::string, std::map<std::string, ecs::Component *>> Container::ComponentsGet()
    {
        return this->Components;
    }

    std::map<std::string, std::map<std::string, ecs::Component *>> Container::ComponentsGet(std::vector<std::string> Types)
    {
        std::map<std::string, std::map<std::string, ecs::Component *>> c;

        for(auto &t : Types) c[t] = this->Components[t];
        return c;
    }

    ecs::Entity *Container::Entity(std::string Handle)
    {
        return this->EntityCreate(Handle);
    }

    ecs::Entity *Container::Entity()
    {
        return this->EntityCreate("");
    }

    ecs::Entity *Container::EntityCreate(std::string Handle)
    {
        ecs::Entity *e;
        if(Handle.size() == 0)
        {
            e = new ecs::Entity();
            this->Entities[e->HandleGet()] = e;
        }
        else
        {
            if(this->Entities.count(Handle) == 0)
            {
                e = new ecs::Entity(Handle);
                this->Entities[Handle] = e;
            }
            else e = this->Entities[Handle];
        }

        e->ContainerSet(this);
        return e;
    }

    void Container::ThreadFunc()
    {
        while(this->ThreadRunning)
        {
            usleep(10000);	
            this->Update();
        }
    }

    void Container::Update()
    {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        uint32_t dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->LastTime).count();
        this->LastTime = now;

        for(auto &Handle : this->SystemsGet())
        {
            this->Systems[Handle]->Update(dt);
        }
    }
}
