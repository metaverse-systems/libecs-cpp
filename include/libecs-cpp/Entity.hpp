#pragma once

#include <string>
#include <memory>
#include <type_traits>
#include <libecs-cpp/Component.hpp>
#include <libecs-cpp/json.hpp>

namespace ecs
{
    class Container;
    class Component;

    /*! One thing in a container, to which components are attached.
     *
     * An entity is a handle and nothing else; its data is in its components. Create one with
     * Container::Entity().
     *
     * Thread: container thread only. An entity changes its container's tables, so every member must be
     * used from the thread that drives the container (see Container). */
    class Entity
    {
      public:
        /*! Creates an entity with a generated handle. Throws std::runtime_error if the container is null.
         *  Applications normally call Container::Entity() instead, which creates the entity and owns it. */
        Entity(ecs::Container *container);
        /*! Creates an entity with the given handle. Throws std::runtime_error if the container is null.
         *  Applications normally call Container::Entity(handle) instead. */
        Entity(ecs::Container *container, const std::string &handle);
        /*! Describes the entity and its components as JSON. */
        nlohmann::json Export() const;
        /*! The container the entity belongs to. */
        ecs::Container *Container;
        /*! The entity's handle, given at construction and never changed. */
        const std::string Handle;
        /*! Attaches a component to this entity and returns the stored component.
         *
         * The call sets the component's EntityHandle and hands it to Container::Component(), which
         * describes ownership, replacement of a component of the same Type, and the rejected inputs. In
         * short: pass std::make_unique<T>(...) or a std::move()d std::unique_ptr; the caller's pointer is
         * empty afterwards; a second component of the same Type replaces the first; and a null pointer,
         * an empty Type or an entity that is no longer in its container throws std::runtime_error. */
        std::shared_ptr<ecs::Component> Component(std::unique_ptr<ecs::Component> component);
        /*! Looks up this entity's component with the given type name, as class T.
         *
         * The result is empty when the entity has no component with that type name or the stored one is
         * not a T. With the default T, ecs::Component, any stored component is returned. The call never
         * throws and never changes the container. See Container::ComponentGet() for the details. */
        template <class T = ecs::Component>
        std::shared_ptr<T> ComponentGet(const std::string &type) const
        {
            const std::shared_ptr<ecs::Component> *found = this->componentFind(type);
            if(found == nullptr)
            {
                return nullptr;
            }
            if constexpr(std::is_same_v<T, ecs::Component>)
            {
                return *found;
            }
            else
            {
                return std::dynamic_pointer_cast<T>(*found);
            }
        }
        /*! Says whether this entity has a component stored under the given type name. Only the name is
         *  compared, so the component's class does not matter. The call never throws and never changes
         *  the container. */
        bool ComponentHas(const std::string &type) const;
        /*! Removes this entity and its components from the container. The entity object is destroyed by
         *  the call, so the pointer must not be used afterwards. */
        void Destroy();
        /*! Removes this entity's component with the given type name. An unknown type name is a silent
         *  no-op. */
        void ComponentDestroy(const std::string &type);
      private:
        const std::shared_ptr<ecs::Component> *componentFind(const std::string &type) const;
    };
}
