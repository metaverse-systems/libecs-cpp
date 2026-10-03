#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <libecs-cpp/json.hpp>

namespace ecs
{
    /*! World thread only: a component lives in its world's tables, so it must be used and changed from
     *  the thread that drives the world (see Container). */
    class Component
    {
      public:
        Component();
        Component(const nlohmann::json &config);
        virtual nlohmann::json Export() const = 0;
        const std::string Handle;
        std::string Type;
        std::string EntityHandle;
        virtual ~Component() = default;
    };

    /*! Components are stored in the world's table as shared pointers. Reading
     *  Components[type][entity] inserts an empty entry when the type or entity is missing, while
     *  Container::ComponentGet() and Entity::ComponentGet() leave the table unchanged. */
    typedef std::unordered_map<std::string, std::shared_ptr<ecs::Component>> EntityComponentList;

    typedef std::unordered_map<std::string, ecs::EntityComponentList> TypeEntityComponentList;
}
