#include <libecs-cpp/Component.hpp>
#include <libecs-cpp/ecs.hpp>
#include <iostream>
#include <vector>

namespace ecs
{
    System::System():
        Handle(ecs::Uuid().Get()) 
    {
    }

    System::System(std::string Handle):
        Handle(Handle) 
    {
    }

    void System::Initialize() {}

    void System::UpdateSystem()
    {
        std::vector<std::string> timersToRemove;
        for(auto &timer : this->Timers)
        {
            if(timer.CallbackRun() && !timer.Repeat)
            {
                timersToRemove.push_back(timer.Name);
            }
        }

        // Remove timers after iteration to avoid modifying container during iteration
        for(const auto &timerName : timersToRemove)
        {
            this->TimerClear(timerName);
        }

        this->Update();
    }

    void System::Configure(nlohmann::json config) {}

    void System::MessageSubmit(nlohmann::json message)
    {
        this->messages.push(message);
    }

    const size_t System::MessagesWaiting()
    {
        return this->messages.size();
    }

    const uint32_t System::DeltaTimeGet()
    {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        uint32_t dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->LastTime).count();
        this->LastTime = now;
        return dt;
    }

    void System::ComponentsClear()
    {
        if (!this->Container)
        {
            std::cout << "Warning: Container is null in ComponentsClear()" << std::endl;
            return;
        }

        for(auto &[type, entities] : this->ComponentsToDelete)
        {
            for(const auto &entity : entities)
            {
                this->Container->Components[type].erase(entity);
            }
        }
        this->ComponentsToDelete.clear();
    }

    void System::TimerClear(std::string name)
    {
        this->Timers.erase(
            std::remove_if(this->Timers.begin(), this->Timers.end(),
                [&name](const Timer &timer) { return timer.Name == name; }),
            this->Timers.end());
    }

    void System::TimerAdd(Timer timer)
    {
        this->Timers.push_back(timer);
    }
}
