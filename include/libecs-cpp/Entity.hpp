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
        Entity(unsigned __int128 uuid);
        Json::Value save();
        void ContainerSet(ecs::Container *Container);
        unsigned __int128 Handle;
        std::string HandleGet();
        ecs::Container *Container = nullptr;
        ecs::ComponentList ComponentsGet();
        std::shared_ptr<ecs::Component> ComponentGet(std::string Type);
        std::shared_ptr<ecs::Component> Component(std::shared_ptr<ecs::Component> c);
        void destroy();
      private:
        ecs::ComponentList Components;
    };
}
