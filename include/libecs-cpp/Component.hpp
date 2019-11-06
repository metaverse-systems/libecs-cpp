#pragma once

#include <json/json.h>
#include <unordered_map>
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
        std::string EntityHandle;
        virtual ~Component() = 0;
    };

    /* 
    ## ComponentList
    
    Contains an array of ecs::Components of a single Type belonging to a single Entity.
    */
    class ComponentList
    {
      public:
        std::string Type = "";
        std::string EntityHandle = "";
        void Push(std::shared_ptr<ecs::Component> c)
        {
            if(!this->Type.size()) 
            {
                this->Type = c->Type;
                this->EntityHandle = c->EntityHandle;
            }
            else
            {
                if(c->Type != this->Type)
                {
                    auto err = "ecs::ComponentList::Push(): Trying to push a component of type '" + c->Type;
                    err += "' onto a list of type '" + this->Type + "'.";
                    throw std::runtime_error(err);
                }
            }
            this->component_list.push_back(c);
        }
        std::shared_ptr<ecs::Component> Pop()
        {
            if(this->component_list.empty()) return nullptr;
            auto c = this->component_list.back();
            this->component_list.pop_back();
            return c;
        }
        std::size_t Size()
        {
            return this->component_list.size();
        }
      private:
        std::vector<std::shared_ptr<ecs::Component>> component_list;
    };

    class _EntityComponentList
    {
      public:
        std::string EntityHandle = "";
/*         ecs::ComponentList ComponentListGet(std::string type)
        {
            if(this->entity_component_list[])
        }
 */      private:
        std::unordered_map<std::string, ecs::ComponentList> entity_component_list;
    };
    typedef std::unordered_map<std::string, ecs::ComponentList> EntityComponentList;

    class _TypeEntityComponentList
    {
      private:
        std::unordered_map<std::string, ecs::_EntityComponentList> type_entity_component_list;
    };
    typedef std::unordered_map<std::string, ecs::EntityComponentList> TypeEntityComponentList;
}
