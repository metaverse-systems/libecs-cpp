#pragma once

#include <unordered_map>
#include <string>
#include <libecs-cpp/json.hpp>

namespace ecs
{
    class Component
    {
      public:
        Component();
        Component(nlohmann::json);
        virtual nlohmann::json Export() = 0;
        const std::string Handle;
        std::string Type;
        std::string EntityHandle;
        virtual ~Component() = 0;
    };

    typedef std::unordered_map<std::string, std::shared_ptr<ecs::Component>> EntityComponentList;

    typedef std::unordered_map<std::string, ecs::EntityComponentList> TypeEntityComponentList;
}
