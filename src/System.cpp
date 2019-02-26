#include <uuid/uuid.h>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    System::System()
    {
        uuid_t uuid;
        uuid_generate(uuid);

        this->Handle.resize(40);
        uuid_unparse(uuid, &this->Handle[0]);
    }

    System::System(std::string Handle)
    {
        this->Handle = Handle;
    }

    std::string System::HandleGet()
    {
        return this->Handle;
    }

    void System::ContainerSet(ecs::Container *container)
    {
        this->Container = container;
    }
}
