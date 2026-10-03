#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <type_traits>
#include <libecs-cpp/Component.hpp>
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
        /*! Attaches a component to this entity, setting its EntityHandle. The world becomes the sole owner
         *  from the moment of the call: the caller's handle is empty afterwards, whether the call is accepted
         *  or rejected, and a rejected component is released exactly once. Pass std::make_unique<T>(...) or
         *  std::move(handle); raw pointers, shared_ptrs and copies of a handle do not compile. Returns the
         *  stored component. An empty handle, an empty Type, and an entity whose Handle names no entity in
         *  its world throw std::runtime_error and leave the world unchanged. A second component of the
         *  same Type replaces the first; holders of the old component keep a valid object. */
        std::shared_ptr<ecs::Component> Component(std::unique_ptr<ecs::Component> component);
        /*! Looks up this entity's component of the given type, as kind T. Empty when the entity has no
         *  component of that type or the stored one is not a T; with the default kind any stored
         *  component is returned. Never throws, never changes the world, takes no lock and never
         *  allocates given an existing std::string. The result stays valid if the component is later
         *  replaced or removed. A short string literal (up to 15 characters) does not allocate when
         *  converted to std::string, a longer one does, so code that runs every pass should hold its
         *  type names in std::string constants. See Container::ComponentGet(). */
        template <class T = ecs::Component>
        std::shared_ptr<T> ComponentGet(const std::string &type) const
        {
            std::shared_ptr<ecs::Component> found = this->componentFind(type);
            if constexpr(std::is_same_v<T, ecs::Component>)
            {
                return found;
            }
            else
            {
                return std::dynamic_pointer_cast<T>(found);
            }
        }
        /*! True when this entity has a component stored under the given type name, whatever its kind
         *  (only the name is compared; an empty slot counts as no component). Never throws,
         *  never changes the world, takes no lock and never allocates given an existing std::string. */
        bool ComponentHas(const std::string &type) const;
        void Destroy();
        void ComponentDestroy(const std::string &type);
    
      private:
        std::shared_ptr<ecs::Component> componentFind(const std::string &type) const;
    };
}
