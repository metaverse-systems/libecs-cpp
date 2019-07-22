#include <uuid/uuid.h>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Entity::Entity()
    {
        uuid_t uuid;
        this->Handle.resize(40);

#ifdef _WIN32
        UuidCreate(&uuid);
        RPC_CSTR szUuid = NULL;
        if(UuidToString(&uuid, &szUuid) == RPC_S_OK)
        {
            this->Handle = (char*) szUuid;
            RpcStringFree(&szUuid);
        }
#else
        uuid_generate(uuid);
        uuid_unparse(uuid, &this->Handle[0]);
#endif
    }

    Entity::Entity(std::string Handle)
    {
        this->Handle = Handle;
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

    std::string Entity::HandleGet()
    {
        return this->Handle;
    }

    void Entity::ContainerSet(ecs::Container *container)
    {
        this->Container = container;
    }

    ecs::ComponentList Entity::ComponentsGet()
    {
        return this->Components;
    }

    ecs::Component *Entity::ComponentGet(std::string Type)
    {
        for(auto &c : this->Components)
        {
            if(c->Type == Type) return c;
        }

        std::string err = "No component of type: " + Type;
        throw std::runtime_error(err); 
    }

    ecs::Component *Entity::Component(ecs::Component *c)
    {
        c->EntityHandle = this->Handle;
        this->Components.push_back(c);
        this->Container->Component(c);
        return c;
    }
}
