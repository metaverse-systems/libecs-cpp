#include <cstring>
#include <libecs-cpp/ecs.hpp>
#include <iostream>
#include <stdexcept>

namespace ecs
{
    Entity::Entity(ecs::Container *container):
        Container(container), Handle(ecs::Uuid().Get())
    {
        if(container == nullptr)
        {
            throw std::runtime_error("ecs::Entity: container is missing.");
        }
    }

    Entity::Entity(ecs::Container *container, const std::string &handle):
        Container(container), Handle(handle)
    {
        if(container == nullptr)
        {
            throw std::runtime_error("ecs::Entity: container is missing.");
        }
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

    std::shared_ptr<ecs::Component> Entity::Component(std::unique_ptr<ecs::Component> component)
    {
        if(!component)
        {
            throw std::runtime_error("ecs::Entity(\"" + this->Handle + "\")::Component(): component is missing.");
        }
        component->EntityHandle = this->Handle;
        // A rejection releases the component once, inside the container call.
        return this->Container->Component(std::move(component));
    }

    std::shared_ptr<ecs::Component> Entity::componentFind(const std::string &type) const
    {
        auto byType = this->Container->Components.find(type);
        if(byType == this->Container->Components.end())
        {
            return nullptr;
        }
        auto byEntity = byType->second.find(this->Handle);
        if(byEntity == byType->second.end())
        {
            return nullptr;
        }
        return byEntity->second;
    }

    bool Entity::ComponentHas(const std::string &type) const
    {
        return this->componentFind(type) != nullptr;
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
