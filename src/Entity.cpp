#include <uuid/uuid.h>
#include <cstring>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Entity::Entity()
    {
        uuid_t uuid;
#ifdef _WIN32
        UuidCreate(&uuid);
#else
        uuid_generate(uuid);
#endif
        std::memcpy(&this->Handle, &uuid, 16);
    }

    Entity::Entity(unsigned __int128 uuid)
    {
        std::memcpy(&this->Handle, &uuid, 16);
    }

    Json::Value Entity::save()
    {
        Json::Value config;

        config["Handle"] = this->HandleGet();
        for(auto &c : this->Components)
        {
            config["Components"][c->Type] = c->save();
        }

        return config;
    }

    void Entity::ContainerSet(ecs::Container *container)
    {
        this->Container = container;
    }

    ecs::ComponentList Entity::ComponentsGet()
    {
        return this->Components;
    }

    std::shared_ptr<ecs::Component> Entity::ComponentGet(std::string Type)
    {
        for(auto &c : this->Components)
        {
            if(c->Type == Type) return c;
        }

        return nullptr;
    }

    std::shared_ptr<ecs::Component> Entity::Component(std::shared_ptr<ecs::Component> c)
    {
        std::memcpy(&c->EntityHandle, &this->Handle, 16);
        this->Components.push_back(c);
        this->Container->Component(c);
        return c;
    }

    void Entity::destroy()
    {
        while (!this->Components.empty())
        {
            this->Components.pop_back();
        }
        this->Container->EntityDestroy(this->Handle);
    }

    std::string Entity::HandleGet()
    {
        uuid_t uuid;
        std::memcpy(&uuid, &this->Handle, 16);

        std::string handle;
        handle.resize(40);
#ifdef _WIN32
        RPC_CSTR szUuid = NULL;
        if(UuidToString(&uuid, &szUuid) == RPC_S_OK)
        {
            handle = (char*) szUuid;
            RpcStringFree(&szUuid);
        }
#else
        uuid_unparse(uuid, &handle[0]);
#endif
        return handle;
    }
}
