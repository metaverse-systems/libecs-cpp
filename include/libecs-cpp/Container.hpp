#pragma once

#include <string>

namespace ecs
{
    class Manager;

    class Container
    {
      public:
        Container();
        Container(std::string Handle);
        void ManagerSet(ecs::Manager *Manager);
        std::string HandleGet();
      private:
        std::string Handle;
        ecs::Manager *Manager = nullptr;
    };
}
