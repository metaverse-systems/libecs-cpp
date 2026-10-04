#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <libecs-cpp/json.hpp>

namespace ecs
{
    /*! Base class for the data attached to an entity.
     *
     * A subclass sets Type in its constructor and implements Export(). A component has no handle of its
     * own: it is addressed by its entity (EntityHandle) and its type name (Type). Attach it with
     * Entity::Component() and look it up with Entity::ComponentGet() or Container::ComponentGet().
     *
     * Thread: container thread only. A component lives in its container's tables, so it must be used and
     * changed from the thread that drives the container (see Container). */
    class Component
    {
      public:
        /*! Creates a component with an empty Type and EntityHandle. */
        Component() = default;

        /*! Creates a component from a configuration, which the base class does not read. A subclass reads
         *  from it whatever it needs. The constructor is not explicit, so a component that forwards its
         *  configuration to the base compiles. */
        Component(const nlohmann::json &) {}

        /*! Describes the component as JSON; every subclass implements it. */
        virtual nlohmann::json Export() const = 0;
        /*! The type name the component is stored and looked up under, for example "PositionComponent".
         *  A subclass sets it in its constructor. A component with an empty Type is rejected when it is
         *  attached. */
        std::string Type;
        /*! The handle of the entity the component is attached to. Entity::Component() sets it;
         *  Container::Component() reads it. */
        std::string EntityHandle;
        /*! Destroys the component. */
        virtual ~Component() = default;
    };

    /*! The components of one type, by entity handle. They are stored as shared pointers. */
    typedef std::unordered_map<std::string, std::shared_ptr<ecs::Component>> EntityComponentList;

    /*! The components of a container, by type name and then by entity handle. Reading
     *  Components[type][entity] inserts an empty entry when the type or entity is missing, while
     *  Container::ComponentGet() and Entity::ComponentGet() leave the table unchanged. */
    typedef std::unordered_map<std::string, ecs::EntityComponentList> TypeEntityComponentList;
}
