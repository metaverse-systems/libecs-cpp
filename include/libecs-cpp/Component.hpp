#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <libecs-cpp/json.hpp>

namespace ecs
{
    /*! A component has no identifier of its own: it is addressed by its entity (EntityHandle) and
     *  its type (Type).
     *
     *  World thread only: a component lives in its world's tables, so it must be used and changed from
     *  the thread that drives the world (see Container). */
    class Component
    {
      public:
        Component() = default;

        /*! The base does not read the configuration. A concrete type reads from it whatever it needs.
         *  The constructor is not explicit, so a component that forwards its configuration to the base
         *  keeps compiling. */
        Component(const nlohmann::json &) {}

        virtual nlohmann::json Export() const = 0;
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
