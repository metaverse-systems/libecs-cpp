#pragma once

#include <string>

namespace ecs
{
    class Component
    {
      public:
        Component();
        std::string Handle;
        std::string Type;
        std::string EntityHandle;
    };
}
