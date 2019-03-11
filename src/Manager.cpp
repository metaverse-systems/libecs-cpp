#include <libecs-cpp/ecs.hpp>

ecs::Manager *ECS = new ecs::Manager();

namespace ecs
{
    Manager::Manager()
    {
    }

    ecs::Container *Manager::Container(std::string handle)
    {
        return this->ContainerCreate(handle);
    }

    ecs::Container *Manager::Container()
    {
        return this->ContainerCreate("");
    }

    bool Manager::IsRunning()
    {
        return Running;
    }

    void Manager::Shutdown()
    {
        this->Running = false;
    }

    ecs::Container *Manager::ContainerCreate(std::string handle)
    {
        ecs::Container *c;
        this->mutex_containers.lock();
        {
            if(handle.size() == 0)
            {
                c = new ecs::Container();
                this->containers[c->HandleGet()] = c;
            }
            else
            {
                if(this->containers.count(handle) == 0)
                {
                    c = new ecs::Container(handle);
                    this->containers[handle] = c;
                }
                else c = this->containers[handle];
            }

            c->ManagerSet(this);
        }
        this->mutex_containers.unlock();
        return c;
    }

    std::vector<std::string> Manager::ContainersGet()
    {
        std::vector<std::string> handles;

        for(auto &c : this->containers)
            handles.push_back(c.first);

        return handles;
    }

    void Manager::ContainersKill(std::vector<std::string> handles)
    {
        this->mutex_containers.lock();
        {
            for(auto &h : handles)
            {
                delete this->containers[h];
            }
        }
        this->mutex_containers.unlock();
    }
}
