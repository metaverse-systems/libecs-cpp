#include <libecs-cpp/ecs.hpp>

ecs::Manager *ECS = new ecs::Manager();

namespace ecs
{
    Manager::Manager()
    {
    }

    Manager::~Manager()
    {
        this->running = false;
    }

    ecs::Container *Manager::Container(const std::string &handle)
    {
        return this->containerCreate(handle);
    }

    ecs::Container *Manager::Container()
    {
        return this->containerCreate(ecs::Uuid().Get());
    }

    bool Manager::IsRunning()
    {
        return running;
    }

    void Manager::Shutdown()
    {
        this->running = false;
    }

    ecs::Container *Manager::containerCreate(const std::string &handle)
    {
        std::lock_guard<std::mutex> lock(this->mutexContainers);
        if(!this->containers.contains(handle))
        {
            this->containers[handle] = std::make_unique<ecs::Container>(this, handle);
        }
        return this->containers[handle].get();
    }

    std::vector<std::string> Manager::ContainersGet()
    {
        std::vector<std::string> handles;

        for(auto &c : this->containers)
            handles.push_back(c.first);

        return handles;
    }

    void Manager::MessageSubmit(const nlohmann::json &message)
    {
        auto dest_container = message["destination"]["container"].get<std::string>();
        if(!this->containers.contains(dest_container))
        {
            auto err = "ecs::Manager::MessageSubmit(): Container " + dest_container + " not found.";
            throw std::runtime_error(err);
        }

        this->containers[dest_container]->MessageSubmit(message);
    }
}
