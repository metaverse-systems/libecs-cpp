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
        std::shared_ptr<ecs::Component> ComponentGet(std::string Type);
        std::shared_ptr<ecs::Component> Component(std::shared_ptr<ecs::Component> c);
        void destroy();
      private:
        ecs::ComponentList Components;
    };
}
