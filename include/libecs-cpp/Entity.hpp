#pragma once

#include <json/json.h>
#include <string>
#include <map>
#include <vector>

namespace ecs
{
    class Container;
    class Component;

    class Entity
    {
      public:
        Entity();
        Entity(std::string Handle);
        Json::Value save();
        void ContainerSet(ecs::Container *Container);
        std::string HandleGet();
        std::string Handle;
        ecs::Container *Container = nullptr;
        std::map<std::string, ecs::Component *> ComponentsGet();
        ecs::Component *ComponentGet(std::string Type);
        ecs::Component *Component(ecs::Component *c);
      private:
        std::map<std::string, ecs::Component *> Components;
    };
}
