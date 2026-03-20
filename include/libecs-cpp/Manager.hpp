#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <memory>
#include <libecs-cpp/json.hpp>

namespace ecs
{
    class Container;

    /** Manages containers. */

    class Manager
    {
      public:
        Manager();
        ~Manager();
        ecs::Container *Container(const std::string &handle);
        ecs::Container *Container();
        std::vector<std::string> ContainersGet();
        bool IsRunning();
        void Shutdown();
        void MessageSubmit(const nlohmann::json &message);
      private:
        ecs::Container *containerCreate(const std::string &handle);
        std::unordered_map<std::string, std::unique_ptr<ecs::Container>> containers;
        std::mutex mutexContainers;
        bool running = true;
    };
}
