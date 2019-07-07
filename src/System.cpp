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

    void System::Init()
    {
    }

    std::string System::HandleGet()
    {
        return this->Handle;
    }

    void System::ContainerSet(ecs::Container *container)
    {
        this->Container = container;
    }

    void System::ComponentRequest(std::string component)
    {
        for(auto &s : this->RequestedComponents)
        {
            if(s == component) return;
        }
        RequestedComponents.push_back(component);
    }

    std::map<std::string, std::map<std::string, ecs::Component *>> System::ComponentsGet()
    {
        return this->Container->ComponentsGet(this->RequestedComponents);
    }
}
