#pragma once

#include <json/json.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace ecs
{
    class Container;
    class Component;

    class Entity
    {
      public:
        Entity();
        Entity(std::string uuid);
        Json::Value Export();
        void ContainerSet(ecs::Container *Container);
        std::string Handle;
        std::string HandleGet();
        ecs::Container *Container = nullptr;
        ecs::TypeEntityComponentList ComponentsGet();
        ecs::TypeEntityComponentList ComponentGet(std::string Type);
        std::shared_ptr<ecs::Component> Component(ecs::Component *component);
        void destroy();
        void ComponentDestroy(std::string Type);
      private:
        ecs::TypeEntityComponentList Components;
    };
}
