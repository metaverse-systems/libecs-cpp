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
        ecs::ComponentList ComponentsGet();
        ecs::Component *ComponentGet(std::string Type);
        ecs::Component *Component(ecs::Component *c);
      private:
        ecs::ComponentList Components;
    };
}
