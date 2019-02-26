#include <uuid/uuid.h>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Entity::Entity()
    {
        uuid_t uuid;
        uuid_generate(uuid);

        this->Handle.resize(40);
        uuid_unparse(uuid, &this->Handle[0]);
    }

    Entity::Entity(std::string Handle)
    {
        this->Handle = Handle;
    }

    std::string Entity::HandleGet()
    {
        return this->Handle;
    }

    void Entity::ContainerSet(ecs::Container *container)
    {
        this->Container = container;
    }

    std::map<std::string, std::map<std::string, ecs::Component *>> Entity::ComponentsGet()
    {
        return this->Components;
    }

    std::map<std::string, std::map<std::string, ecs::Component *>> Entity::ComponentsGet(std::vector<std::string> Types)
    {
        std::map<std::string, std::map<std::string, ecs::Component *>> c;

        for(auto &t : Types) c[t] = this->Components[t];
        return c;
    }

    ecs::Component *Entity::Component(ecs::Component *c)
    {
        c->EntityHandle = this->Handle;
        this->Components[c->Type][c->Handle] = c;
        this->Container->Component(c);
        return c;
    }
}
