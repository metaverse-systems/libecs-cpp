#pragma once

#include <string>
#include <map>
#include <vector>

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
      private:
        void ContainersKill(std::vector<std::string>);
        ecs::Container *ContainerCreate(std::string handle);
        std::map<std::string, ecs::Container *> containers;
    };
}
