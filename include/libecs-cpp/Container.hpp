#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#if _WIN32
  #include <mingw.thread.h>
#else
  #include <thread>
#endif

#include <unistd.h>
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
        Container(ecs::Manager *Manager);
        Container(ecs::Manager *Manager, std::string Handle);
        void Start();
        void Start(uint32_t);
        void SystemsInit();
        ecs::System *System(ecs::System *);
        std::vector<std::string> SystemsGet();
        std::shared_ptr<ecs::Component> Component(std::shared_ptr<ecs::Component> c);
        void ComponentDestroy(std::string entity, std::string Type);
        ecs::Entity *Entity(std::string);
        ecs::Entity *Entity();
        void EntityDestroy(std::string);
        nlohmann::json Export();
        void Update();
        void MessageSubmit(nlohmann::json);
        void ResourceAdd(std::string, ecs::Resource);
        ecs::Resource ResourceGet(std::string);
        std::unordered_map<std::string, ecs::Entity *> Entities;
        ecs::Manager *Manager = nullptr;
        const std::string Handle;
        ecs::TypeEntityComponentList Components;
      private:
        /*! Number of microseconds to sleep between Update() calls */
        uint32_t sleep_interval = 1000000 / 30;

        /*! A unique ID for the container */
        std::unordered_map <std::string, ecs::System *> Systems;
        std::thread ContainerThread;
        void ThreadFunc();
        bool ThreadRunning = true;
        ecs::Entity *EntityCreate(std::string);

        std::unordered_map<std::string, ecs::Resource> resources;
    };
}
