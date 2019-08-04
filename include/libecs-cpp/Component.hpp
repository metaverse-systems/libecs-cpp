#pragma once

#include <uuid/uuid.h>
#include <json/json.h>
#include <map>
#include <memory>
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
        unsigned __int128 EntityHandle;
    };

    typedef std::vector<std::shared_ptr<ecs::Component>> ComponentList;
}
