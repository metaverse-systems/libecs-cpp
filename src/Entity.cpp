#include <cstring>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Entity::Entity()
    {
        this->Handle = ecs::Uuid().Get();
    }

    Entity::Entity(std::string uuid)
    {
        this->Handle = uuid;
    }

    Json::Value Entity::Export()
    {
        Json::Value config;

        config["Handle"] = this->HandleGet();
        for(auto &[type, entity_component_list] : this->Components)
        {
            for(auto &[entity, component] : entity_component_list)
            {
                config["Components"][type] = component->Export(); 
            }
        }

        return config;
    }

    void Entity::ContainerSet(ecs::Container *container)
    {
        this->Container = container;
    }

    ecs::TypeEntityComponentList Entity::ComponentsGet()
    {
        return this->Components;
    }

    ecs::TypeEntityComponentList Entity::ComponentGet(std::string Type)
    {
        ecs::TypeEntityComponentList result;
        result[Type] = this->Components[Type];

        return result;
    }

    std::shared_ptr<ecs::Component> Entity::Component(ecs::Component *component)
    {
        std::shared_ptr<ecs::Component> c(component);
        c->EntityHandle = this->Handle;
        this->Components[c->Type][c->EntityHandle] = c;
        this->Container->Component(c);
        return c;
    }

    void Entity::ComponentDestroy(std::string Type)
    {
        this->Components.erase(Type);
        this->Container->ComponentDestroy(this->Handle, Type);
    }

    void Entity::destroy()
    {
        this->Container->EntityDestroy(this->Handle);
    }

    std::string Entity::HandleGet()
    {
        return this->Handle;
    }
}
