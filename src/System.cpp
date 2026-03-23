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

    System::System(const std::string &handle):
        Handle(handle) 
    {
    }

    void System::Initialize() {}

    void System::UpdateSystem()
    {
        std::vector<std::string> timersToRemove;
        for(auto &timer : this->timers)
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

    void System::Configure(const nlohmann::json &/* config */) {}

    void System::MessageSubmit(const nlohmann::json &message)
    {
        this->messages.push(message);
    }

    size_t System::MessagesWaiting()
    {
        return this->messages.size();
    }

    uint32_t System::DeltaTimeGet()
    {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        uint32_t dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->lastTime).count();
        this->lastTime = now;
        return dt;
    }

    void System::componentsClear()
    {
        if (!this->Container)
        {
            std::cout << "Warning: Container is null in componentsClear()" << std::endl;
            return;
        }

        for(auto &[type, entities] : this->componentsToDelete)
        {
            for(const auto &entity : entities)
            {
                this->Container->Components[type].erase(entity);
            }
        }
        this->componentsToDelete.clear();
    }

    void System::TimerClear(const std::string &name)
    {
        this->timers.erase(
            std::remove_if(this->timers.begin(), this->timers.end(),
                [&name](const Timer &timer) { return timer.Name == name; }),
            this->timers.end());
    }

    void System::TimerAdd(Timer timer)
    {
        this->timers.push_back(timer);
    }

    void System::Log(const std::string &message, const std::string &level)
    {
        this->Container->Log("[" + this->Handle + "] " + message, level);
    }
}
