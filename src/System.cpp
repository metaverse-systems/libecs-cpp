#include <libecs-cpp/Component.hpp>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    System::System():
        Handle(ecs::Uuid().Get()) {}

    System::System(std::string Handle):
        Handle(Handle) {}

    void System::Initialize() {}

    void System::Configure(nlohmann::json config) {}

    void System::MessageSubmit(nlohmann::json message)
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
