#include <libecs-cpp/Component.hpp>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    System::System()
    {
        this->Handle = ecs::Uuid().Get();
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

    ecs::Container *System::ContainerGet()
    {
        return this->Container;
    }

    void System::ComponentRequest(std::string component)
    {
        for(auto &s : this->RequestedComponents)
        {
            if(s == component) return;
        }
        RequestedComponents.push_back(component);
    }

    ecs::TypeEntityComponentList System::ComponentsGet()
    {
        return this->Container->ComponentsGet(this->RequestedComponents);
    }

    void System::MessageSubmit(Json::Value message)
    {
        this->messages.push(message);
    }

    uint32_t System::DeltaTimeGet()
    {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        uint32_t dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->LastTime).count();
        this->LastTime = now;
        return dt;
    }
}
