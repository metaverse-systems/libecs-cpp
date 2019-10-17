#include <libecs-cpp/ecs.hpp>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <uuid/uuid.h>
#ifdef WITHGPERFTOOLS
#include <gperftools/profiler.h>
#endif

namespace ecs
{
    Container::Container()
    {
        this->Handle.resize(40);

#ifdef _WIN32
        UUID uuid;
        UuidCreate(&uuid);
        RPC_CSTR szUuid = NULL;
        if(UuidToString(&uuid, &szUuid) == RPC_S_OK)
        {
            this->Handle = (char*) szUuid;
            RpcStringFree(&szUuid);
        }
#else
        uuid_t uuid;
        uuid_generate(uuid);
        uuid_unparse(uuid, &this->Handle[0]);
#endif
    }

    Container::Container(std::string Handle)
    {
        this->Handle = Handle;
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

    Json::Value Container::save()
    {
        Json::Value data;

        data["handle"] = this->Handle;

        for(auto &e : this->Entities)
        {
            ecs::Entity *entity = this->Entities[e.first];

            std::string handle;
            
#ifdef _WIN32
            UUID uuid;
            RPC_CSTR szUuid = NULL;
            if(UuidToString(&uuid, &szUuid) == RPC_S_OK)
            {
                handle = (char*) szUuid;
                RpcStringFree(&szUuid);
            }
#else
            uuid_t uuid;
            std::memcpy(&uuid, &e.first, 16);
            uuid_unparse(uuid, &handle[0]);
#endif
            data["entities"][handle] = entity->save();
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

    std::shared_ptr<ecs::Component> Container::Component(std::shared_ptr<ecs::Component> c)
    {
        this->Components[c->Type][c->EntityHandle].push_back(c); 
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
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        uint32_t dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->LastTime).count();
        this->LastTime = now;

        for(auto &Handle : this->SystemsGet())
        {
            this->Systems[Handle]->Update(dt);
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
    }
}
