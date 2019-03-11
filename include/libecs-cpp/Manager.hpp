#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>

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
      private:
        void ContainersKill(std::vector<std::string>);
        ecs::Container *ContainerCreate(std::string handle);
        std::map<std::string, ecs::Container *> containers;
        std::mutex mutex_containers;
        bool Running = true;
    };
}
