#pragma once

#include <json/json.h>
#include <map>
#include <string>

namespace ecs
{
    class Component
    {
      public:
        Component();
        Component(Json::Value);
        virtual Json::Value save() = 0;
        std::string Handle;
        std::string Type;
        std::string EntityHandle;
    };

    typedef std::vector<ecs::Component *> ComponentList;
}
