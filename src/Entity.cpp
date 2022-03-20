#include <cstring>
#include <libecs-cpp/ecs.hpp>
#include <iostream>

namespace ecs
{
    Entity::Entity(ecs::Container *Container):
        Container(Container), Handle(ecs::Uuid().Get())
    {
    }

    Entity::Entity(ecs::Container *Container, std::string Handle):
        Container(Container), Handle(Handle)
    {
    }

    nlohmann::json Entity::Export()
    {
        nlohmann::json config;

        config["Handle"] = this->Handle;
        for(auto &[type, entity_component_list] : this->Container->Components)
        {
            config["Components"][type] = entity_component_list[this->Handle]->Export();
        }

        return config;
    }

    std::shared_ptr<ecs::Component> Entity::Component(ecs::Component *component)
    {
        std::shared_ptr<ecs::Component> c(component);
        c->EntityHandle = this->Handle;
        this->Container->Component(c);
        return c;
    }

    void Entity::ComponentDestroy(std::string Type)
    {
        this->Container->ComponentDestroy(this->Handle, Type);
    }

    void Entity::Destroy()
    {
        this->Container->EntityDestroy(this->Handle);
    }
}
