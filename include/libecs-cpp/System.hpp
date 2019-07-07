#pragma once

#include <string>

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
        std::map<std::string, std::map<std::string, ecs::Component *>> ComponentsGet();
        void ComponentRequest(std::string);
      private:
        std::vector<std::string> RequestedComponents;
    };
}
