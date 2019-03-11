#pragma once

#include <jsoncpp/json/json.h>
#include <string>

namespace ecs
{
    class Component
    {
      public:
        Component();
        virtual Json::Value save() = 0;
        std::string Handle;
        std::string Type;
        std::string EntityHandle;
    };
}
