#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <libecs-cpp/json.hpp>

namespace ecs
{
    class Container;
    class Component;

    /*! World thread only: an entity changes its world's tables, so every member must be used from the
     *  thread that drives the world (see Container). */
    class Entity
    {
      public:
        /*! Both constructors throw std::runtime_error if the container is null. */
        Entity(ecs::Container *container);
        Entity(ecs::Container *container, const std::string &handle);
        nlohmann::json Export() const;
        ecs::Container *Container;
        const std::string Handle;
        /*! Attaches a component to this entity, setting its EntityHandle. The library owns the raw pointer
         *  from the moment of the call, including when the call is rejected, in which case the component is
         *  deleted. A null pointer, an empty Type, and an entity whose Handle names no entity in its world throw
         *  std::runtime_error and leave the world unchanged. A second component of the same Type replaces
         *  the first; holders of the old component keep a valid object. */
        std::shared_ptr<ecs::Component> Component(ecs::Component *component);
        void Destroy();
        void ComponentDestroy(const std::string &type);
    };
}
