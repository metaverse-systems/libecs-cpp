#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <libecs-cpp/json.hpp>

namespace ecs
{
    class Container;
    class Component;

    class Entity
    {
      public:
        Entity(ecs::Container *Container);
        Entity(ecs::Container *Container, std::string Handle);
        nlohmann::json Export();
        ecs::Container *Container;
        const std::string Handle;
        std::shared_ptr<ecs::Component> Component(ecs::Component *component);
        void Destroy();
        void ComponentDestroy(std::string Type);
    };
}
