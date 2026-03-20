#include <cstring>
#include <libecs-cpp/ecs.hpp>
#include <iostream>

namespace ecs
{
    Entity::Entity(ecs::Container *container):
        Container(container), Handle(ecs::Uuid().Get())
    {
    }

    Entity::Entity(ecs::Container *container, const std::string &handle):
        Container(container), Handle(handle)
    {
    }

    nlohmann::json Entity::Export() const
    {
        nlohmann::json config;

        config["Handle"] = this->Handle;
        for(auto &[type, entity_component_list] : this->Container->Components)
        {
            auto it = entity_component_list.find(this->Handle);
            if(it != entity_component_list.end() && it->second)
            {
                config["Components"][type] = it->second->Export();
            }
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

    void Entity::ComponentDestroy(const std::string &type)
    {
        this->Container->ComponentDestroy(this->Handle, type);
    }

    void Entity::Destroy()
    {
        this->Container->EntityDestroy(this->Handle);
    }
}
