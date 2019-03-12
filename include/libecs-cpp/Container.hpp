#pragma once

#include <json/json.h>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <chrono>

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
        void ManagerSet(ecs::Manager *Manager);
        ecs::Manager *ManagerGet();
        std::string HandleGet();
        ecs::System *System(ecs::System *);
        std::vector<std::string> SystemsGet();
        ecs::Component *Component(ecs::Component *c);
        std::map<std::string, std::map<std::string, ecs::Component *>> ComponentsGet();
        std::map<std::string, std::map<std::string, ecs::Component *>> ComponentsGet(std::vector<std::string>);
        ecs::Entity *Entity(std::string);
        ecs::Entity *Entity();
        Json::Value save();
      private:
        std::string Handle;
        ecs::Manager *Manager = nullptr;
        std::map <std::string, ecs::System *> Systems;
        std::map<std::string, std::map<std::string, ecs::Component *>> Components;
        std::thread ContainerThread;
        void ThreadFunc();
        bool ThreadRunning = true;
        void Update();
        ecs::Entity *EntityCreate(std::string Handle);
        std::chrono::steady_clock::time_point LastTime = std::chrono::steady_clock::now();
        std::map<std::string, ecs::Entity *> Entities;
    };
}
