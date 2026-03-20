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
        Entity(ecs::Container *container);
        Entity(ecs::Container *container, const std::string &handle);
        nlohmann::json Export() const;
        ecs::Container *Container;
        const std::string Handle;
        std::shared_ptr<ecs::Component> Component(ecs::Component *component);
        void Destroy();
        void ComponentDestroy(const std::string &type);
    };
}
