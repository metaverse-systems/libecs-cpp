#include <cstring>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Entity::Entity(ecs::Container *Container):
        Handle(ecs::Uuid().Get()), Container(Container)
    {
    }

    Entity::Entity(ecs::Container *Container, std::string Handle):
        Handle(Handle), Container(Container)
    {
    }

    Json::Value Entity::Export()
    {
        Json::Value config;

        config["Handle"] = this->Handle;
        for(auto &[type, entity_component_list] : this->Components)
        {
            for(auto &[entity, component] : entity_component_list)
            {
                config["Components"][type] = component->Export(); 
            }
        }

        return config;
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

    void Entity::Destroy()
    {
        this->Container->EntityDestroy(this->Handle);
    }
}
