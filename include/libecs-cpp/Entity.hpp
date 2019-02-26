#pragma once

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
        void ContainerSet(ecs::Container *Container);
        std::string HandleGet();
        std::string Handle;
        ecs::Container *Container = nullptr;
        std::map<std::string, std::map<std::string, ecs::Component *>> ComponentsGet();
        std::map<std::string, std::map<std::string, ecs::Component *>> ComponentsGet(std::vector<std::string> Types);
        ecs::Component *Component(ecs::Component *c);
      private:
        std::map<std::string, std::map<std::string, ecs::Component *>> Components;
    };
}
