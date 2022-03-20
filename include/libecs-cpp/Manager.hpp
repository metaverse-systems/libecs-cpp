#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#ifdef _WIN32
    #include <mingw.mutex.h>
#else
    #include <mutex>
#endif
#include <libecs-cpp/json.hpp>

namespace ecs
{
    class Container;

    /** Manages containers. */

    class Manager
    {
      public:
        Manager();
        ecs::Container *Container(std::string handle);
        ecs::Container *Container();
        std::vector<std::string> ContainersGet();
        bool IsRunning();
        void Shutdown();
        void MessageSubmit(nlohmann::json);
      private:
        void ContainersKill(std::vector<std::string>);
        ecs::Container *ContainerCreate(std::string handle);
        std::unordered_map<std::string, ecs::Container *> containers;
        std::mutex mutex_containers;
        bool Running = true;
    };
}
