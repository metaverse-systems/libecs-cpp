#include <libecs-cpp/ecs.hpp>

#include "Validation.hpp"

ecs::Manager *ECS = new ecs::Manager();

namespace ecs
{
    Manager::Manager()
    {
    }

    Manager::~Manager()
    {
        this->Shutdown();

        std::unordered_map<std::string, std::unique_ptr<ecs::Container>> doomed;
        {
            std::unique_lock<std::mutex> lock(this->mutexContainers);
            this->closing = true;
            this->sendsIdle.wait(lock, [this] { return this->sendsInFlight == 0; });
            doomed = std::move(this->containers);
        }
        doomed.clear();

        // Worlds are gone without the lock held; sends that arrived meanwhile were refused
        // under the lock. Taking it once more means every such refusal has finished with the
        // manager's members before the object's storage can be released.
        std::lock_guard<std::mutex> lock(this->mutexContainers);
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
        return this->running;
    }

    void Manager::Shutdown()
    {
        this->running = false;

        // The worlds are listed under the lock and asked to stop without it, so no lock is held while
        // a world runs user code.
        std::vector<ecs::Container *> threaded;
        {
            std::lock_guard<std::mutex> lock(this->mutexContainers);
            for(auto &entry : this->containers) threaded.push_back(entry.second.get());
        }

        std::erase_if(threaded, [](ecs::Container *world) { return !world->managerStopRequest(); });
        for(auto *world : threaded) world->managerStopWait();
    }

    ecs::Container *Manager::containerCreate(const std::string &handle)
    {
        std::lock_guard<std::mutex> lock(this->mutexContainers);
        if(!this->containers.contains(handle))
        {
            if(this->closing)
            {
                throw std::runtime_error("ecs::Manager::Container(): the manager is shutting down, container " +
                                         handle + " was not created.");
            }
            this->containers[handle] = std::make_unique<ecs::Container>(this, handle);
        }
        return this->containers[handle].get();
    }

    std::vector<std::string> Manager::ContainersGet()
    {
        std::lock_guard<std::mutex> lock(this->mutexContainers);
        std::vector<std::string> handles;

        for(auto &c : this->containers)
            handles.push_back(c.first);

        return handles;
    }

    void Manager::MessageSubmit(const nlohmann::json &message)
    {
        const validation::Caller caller{nullptr};
        const std::string &dest_container = validation::messageContainer(message, caller);
        validation::messageSystem(message, caller);
        ecs::Container *target = nullptr;
        {
            std::lock_guard<std::mutex> lock(this->mutexContainers);
            auto it = this->containers.find(dest_container);
            if(!this->closing && it != this->containers.end())
            {
                target = it->second.get();
                this->sendsInFlight++;
            }
        }

        if(target == nullptr)
        {
            auto err = "ecs::Manager::MessageSubmit(): Container " + dest_container + " not found.";
            throw std::runtime_error(err);
        }

        struct SendDone
        {
            Manager *manager;
            ~SendDone()
            {
                std::lock_guard<std::mutex> lock(this->manager->mutexContainers);
                this->manager->sendsInFlight--;
                if(this->manager->sendsInFlight == 0 && this->manager->closing)
                    this->manager->sendsIdle.notify_all();
            }
        } done{this};

        target->MessageSubmit(message);
    }
}
