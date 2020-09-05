#pragma once

#include <json/json.h>
#include <unordered_map>
#include <memory>
#include <string>

namespace ecs
{
    class Component
    {
      public:
        Component();
        Component(Json::Value);
        virtual Json::Value Export() = 0;
        std::string Handle;
        std::string Type;
        std::string EntityHandle;
        virtual ~Component() = 0;
    };

    typedef std::unordered_map<std::string, std::shared_ptr<ecs::Component>> EntityComponentList;

    typedef std::unordered_map<std::string, ecs::EntityComponentList> TypeEntityComponentList;
}
