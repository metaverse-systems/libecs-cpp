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
        return this->ContainerCreate(ecs::Uuid().Get());
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
        this->mutex_containers.lock();
        {
            if(this->containers.count(handle) == 0)
            {
                this->containers[handle] = new ecs::Container(this, handle);
            }
        }
        this->mutex_containers.unlock();
        return this->containers[handle];
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

    void Manager::MessageSubmit(nlohmann::json message)
    {
        auto dest_container = message["destination"]["container"].get<std::string>();
        if(!this->containers[dest_container])
        {
            auto err = "ecs::Manager::MessageSubmit(): Container " + dest_container + " not found.";
            throw std::runtime_error(err);
        }

        this->containers[dest_container]->MessageSubmit(message);
    }
}
