#include <uuid/uuid.h>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Container::Container()
    {
        uuid_t uuid;
        uuid_generate(uuid);

        this->Handle.resize(40);
        uuid_unparse(uuid, &this->Handle[0]);

//        this->container_thread = std::thread(&container::thread_func, this);
//        this->container_thread.detach();
    }

    Container::Container(std::string Handle)
    {
        this->Handle = Handle;
//        this->ContainerThread = std::thread(&container::thread_func, this);
//        this->ContainerThread.detach();
    }

    void Container::ManagerSet(ecs::Manager *Manager)
    {
        this->Manager = Manager;
    }

    std::string Container::HandleGet()
    {
        return this->Handle;
    }
}
