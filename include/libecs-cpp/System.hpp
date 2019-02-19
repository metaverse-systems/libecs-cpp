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
        void ContainerSet(ecs::Container *Container);
        std::string HandleGet();
        virtual void Update(uint32_t dt) {};
        std::string Handle;
        ecs::Container *Container = nullptr;
    };
}
