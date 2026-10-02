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

    void System::UpdateSystem()
    {
        this->timerWalkDepth++;
        try
        {
            for(size_t i = 0, count = this->timers.size(); i < count && !this->removed; i++)
            {
                // Callbacks may add or cancel timers, so never hold a reference across fire().
                if(this->timers[i].discarded || !this->timers[i].due()) continue;
                if(!this->timers[i].Repeat)
                {
                    this->timers[i].discarded = true;
                    this->timersDiscarded = true;
                }
                this->timers[i].fire();
            }
        }
        catch(...)
        {
            this->timerWalkFinish();
            throw;
        }
        this->timerWalkFinish();

        if(!this->removed) this->Update();
    }

    void System::timerWalkFinish()
    {
        this->timerWalkDepth--;
        if(this->timerWalkDepth > 0) return;

        if(this->timersDiscarded)
        {
            std::erase_if(this->timers, [](const Timer &timer) { return timer.discarded; });
            this->timersDiscarded = false;
        }
        for(auto &timer : this->timersAdded)
        {
            this->timers.push_back(std::move(timer));
        }
        this->timersAdded.clear();
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
        // Copy first: the caller may pass the name of a timer that is erased here.
        const std::string target = name;
        std::erase_if(this->timersAdded, [&target](const Timer &timer) { return timer.Name == target; });
        if(this->timerWalkDepth > 0)
        {
            for(auto &timer : this->timers)
            {
                if(timer.Name == target)
                {
                    timer.discarded = true;
                    this->timersDiscarded = true;
                }
            }
        }
        else
        {
            std::erase_if(this->timers, [&target](const Timer &timer) { return timer.Name == target; });
        }
    }

    void System::TimerAdd(Timer timer)
    {
        if(this->timerWalkDepth > 0)
        {
            this->timersAdded.push_back(std::move(timer));
        }
        else
        {
            this->timers.push_back(std::move(timer));
        }
    }

    void System::Log(const std::string &message, const std::string &level)
    {
        if (!this->Container)
        {
            this->bufferedLogMessages.emplace_back(message, level);
            return;
        }

        for(const auto &[msg, lvl] : this->bufferedLogMessages)
        {
            this->Container->Log("[" + this->Handle + "] " + msg, lvl);
        }
        this->bufferedLogMessages.clear();
        
        this->Container->Log("[" + this->Handle + "] " + message, level);
    }
}
