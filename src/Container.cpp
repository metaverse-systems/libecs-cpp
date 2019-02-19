#include <uuid/uuid.h>
#include <libecs-cpp/ecs.hpp>
#include <thread>
#include <mutex>
#include <chrono>
#include <unistd.h>

namespace ecs
{
    Container::Container()
    {
        uuid_t uuid;
        uuid_generate(uuid);

        this->Handle.resize(40);
        uuid_unparse(uuid, &this->Handle[0]);

        this->ContainerThread = std::thread(&Container::ThreadFunc, this);
        this->ContainerThread.detach();
    }

    Container::Container(std::string Handle)
    {
        this->Handle = Handle;
        this->ContainerThread = std::thread(&Container::ThreadFunc, this);
        this->ContainerThread.detach();
    }

    void Container::ManagerSet(ecs::Manager *Manager)
    {
        this->Manager = Manager;
    }

    std::string Container::HandleGet()
    {
        return this->Handle;
    }

    ecs::System *Container::System(ecs::System *system)
    {
        system->ContainerSet(this);
        this->Systems[system->HandleGet()] = system;
        return system;
    }

    std::vector<std::string> Container::SystemsGet()
    {
        std::vector<std::string> Handles;

        for(auto &system : this->Systems)
            Handles.push_back(system.first);

        return Handles;
    }

    void Container::ThreadFunc()
    {
        while(this->ThreadRunning)
        {
            usleep(10000);	
            this->Update();
        }
    }

    void Container::Update()
    {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        uint32_t dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->LastTime).count();
        this->LastTime = now;

        this->SystemsMutex.lock();
        {
            for(auto &Handle : this->SystemsGet())
            {
                this->Systems[Handle]->Update(dt);
            }
        }
        this->SystemsMutex.unlock();
    }
}
