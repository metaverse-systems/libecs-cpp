#pragma once

#include <json/json.h>
#include <string>
#include <vector>
#include <unordered_map>

#if _WIN32
  #include <mingw.thread.h>
#else
  #include <thread>
#endif

#include <unistd.h>
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
        Container();
        Container(std::string Handle);
        void Start();
        void Start(uint32_t);
        void SystemsInit();
        void ManagerSet(ecs::Manager *Manager);
        ecs::Manager *ManagerGet();
        std::string HandleGet();
        ecs::System *System(ecs::System *);
        std::vector<std::string> SystemsGet();
        std::shared_ptr<ecs::Component> Component(std::shared_ptr<ecs::Component> c);
        ecs::TypeEntityComponentList ComponentsGet();
        ecs::TypeEntityComponentList ComponentsGet(std::vector<std::string>);
        ecs::Entity *Entity(std::string);
        ecs::Entity *Entity();
        void EntityDestroy(std::string);
        Json::Value Export();
        void Update();
        void MessageSubmit(Json::Value);
      private:
        /*! Number of microseconds to sleep between Update() calls */
        uint32_t sleep_interval = 1000000 / 30;

        /*! A unique ID for the container */
        std::string Handle;
        ecs::Manager *Manager = nullptr;
        std::unordered_map <std::string, ecs::System *> Systems;
        ecs::TypeEntityComponentList Components;
        std::thread ContainerThread;
        void ThreadFunc();
        bool ThreadRunning = true;
        ecs::Entity *EntityCreate(std::string);
        std::unordered_map<std::string, ecs::Entity *> Entities;
    };
}
