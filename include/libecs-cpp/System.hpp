#pragma once

#include <string>
#include <queue>

namespace ecs
{
    class Container;

    class System
    {
      public:
        System();
        System(std::string Handle);
        virtual void Init();
        void ContainerSet(ecs::Container *Container);
        std::string HandleGet();
        virtual void Update(uint32_t dt) {};
        std::string Handle;
        ecs::Container *Container = nullptr;
        std::map<std::string, ecs::ComponentList> ComponentsGet();
        void ComponentRequest(std::string);
        void MessageSubmit(Json::Value);
      protected:
        std::vector<std::string> RequestedComponents;
        std::queue<Json::Value> messages;
    };
}
