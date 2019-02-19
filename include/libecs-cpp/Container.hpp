#pragma once

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <chrono>

namespace ecs
{
    class Manager;
    class System;

    class Container
    {
      public:
        Container();
        Container(std::string Handle);
        void ManagerSet(ecs::Manager *Manager);
        std::string HandleGet();
        ecs::System *System(ecs::System *);
        std::vector<std::string> SystemsGet();
      private:
        std::string Handle;
        ecs::Manager *Manager = nullptr;
        std::map <std::string, ecs::System *> Systems;
        std::thread ContainerThread;
        void ThreadFunc();
        bool ThreadRunning = true;
        void Update();
        std::mutex SystemsMutex;
        std::chrono::steady_clock::time_point LastTime = std::chrono::steady_clock::now();
    };
}
