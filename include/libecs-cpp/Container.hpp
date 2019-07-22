#pragma once

#include <json/json.h>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <chrono>
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
        ecs::Component *Component(ecs::Component *c);
        std::map<std::string, ecs::ComponentList> ComponentsGet();
        std::map<std::string, ecs::ComponentList> ComponentsGet(std::vector<std::string>);
        ecs::Entity *Entity(std::string);
        ecs::Entity *Entity();
        Json::Value save();
        void Update();
        void MessageSubmit(Json::Value);
      private:
        uint32_t sleep_interval = 1000000 / 30;
        std::string Handle;
        ecs::Manager *Manager = nullptr;
        std::map <std::string, ecs::System *> Systems;
        std::map<std::string, ecs::ComponentList> Components;
        std::thread ContainerThread;
        void ThreadFunc();
        bool ThreadRunning = true;
        ecs::Entity *EntityCreate(std::string Handle);
        std::chrono::steady_clock::time_point LastTime = std::chrono::steady_clock::now();
        std::map<std::string, ecs::Entity *> Entities;
    };
}
