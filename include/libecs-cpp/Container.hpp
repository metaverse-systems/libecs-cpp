#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <memory>
#include <iostream>
#include <functional>
#include <libecs-cpp/json.hpp>
#include <libecs-cpp/Resource.hpp>
#include <libecs-cpp/Component.hpp>

namespace ecs
{
    class Manager;
    class System;
    class Component;
    class Entity;

    class Container
    {
      public:
        Container(ecs::Manager *manager);
        Container(ecs::Manager *manager, const std::string &handle);
        ~Container();
        void Start();
        void Start(uint32_t);
        void SystemsInitialize();
        ecs::System *System(std::unique_ptr<ecs::System> system);
        std::shared_ptr<ecs::Component> Component(std::shared_ptr<ecs::Component> c);
        void ComponentDestroy(const std::string &entity, const std::string &type);
        ecs::Entity *Entity(const std::string &handle);
        ecs::Entity *Entity();
        void EntityDestroy(const std::string &handle);
        void SystemDestroy(const std::string &handle);
        nlohmann::json Export() const;
        void Update();
        void MessageSubmit(const nlohmann::json &message);
        void ResourceAdd(const std::string &name, ecs::Resource r);
        void Resources(const std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> &resources);
        ecs::Resource ResourceGet(const std::string &name);
        std::unordered_map<std::string, std::unique_ptr<ecs::Entity>> Entities;
        ecs::Manager *Manager = nullptr;
        const std::string Handle;
        ecs::TypeEntityComponentList Components;
        ecs::Uuid UuidGet();
        std::unordered_map<std::string, std::unique_ptr<ecs::System>> Systems;
        void Log(const std::string &message, const std::string &level = "info");
        void LoggerSet(std::function<void(const std::string &, const std::string &)> fn);
        
      private:
        std::function<void(const std::string &, const std::string &)> logger;
        /*! One position in the update order. A null system marks a system removed during the current walk. */
        struct SystemSlot
        {
            std::string handle;
            ecs::System *system;
        };
        class WalkScope;
        std::vector<SystemSlot> system_order;
        uint32_t walkDepth = 0;
        bool orderHasGaps = false;
        void walkFinish();
        /*! Systems removed during a walk. They are released when the outermost walk ends. Declared after Systems so they are destroyed first. */
        std::vector<std::unique_ptr<ecs::System>> retiredSystems;
        void systemRetire(std::unique_ptr<ecs::System> system);
        /*! Number of microseconds to sleep between Update() calls */
        uint32_t sleepInterval = 1000000 / 30;

        std::jthread containerThread;
        void threadFunc(std::stop_token stopToken);
        ecs::Entity *entityCreate(const std::string &handle);

        std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> resources;
    };
}
