#include <libecs-cpp/Component.hpp>
#include <libecs-cpp/ecs.hpp>
#include <iostream>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <string>

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
        this->updateSystem(this->clock->Now());
    }

    void System::updateSystem(std::chrono::microseconds now)
    {
        this->elapsedMeasure(now);
        this->mailboxDrain();
        this->timerWalkDepth++;
        try
        {
            for(size_t i = 0, count = this->timers.size(); i < count && !this->removed; i++)
            {
                // Callbacks may add or cancel timers, so never hold a reference across fire().
                if(this->timers[i].discarded || !this->timers[i].due(now)) continue;
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
        std::lock_guard<std::mutex> guard(this->mailbox->lock);
        this->mailbox->pending.push_back(message);
        this->mailbox->count.store(this->mailbox->pending.size());
    }

    void System::mailboxDrain()
    {
        if(this->mailbox->count.load() == 0) return;
        {
            std::lock_guard<std::mutex> guard(this->mailbox->lock);
            std::swap(this->mailbox->pending, this->staging);
            this->mailbox->count.store(0);
        }
        for(auto &message : this->staging)
        {
            this->messages.push(std::move(message));
        }
        this->staging.clear();
    }

    size_t System::MessagesWaiting()
    {
        this->mailboxDrain();
        return this->messages.size();
    }

    void System::elapsedMeasure(std::chrono::microseconds now)
    {
        if(!this->updated)
        {
            // Nothing earlier to measure from: report the configured interval.
            this->elapsed = this->Timing.GetInterval();
            this->previousUpdate = now;
            this->updated = true;
        }
        else
        {
            // The clock never goes backwards; an instant that did would count as no time passed.
            this->elapsed = std::max(std::chrono::microseconds(0), now - this->previousUpdate);
            this->previousUpdate = std::max(this->previousUpdate, now);
        }

        const std::chrono::microseconds total = this->millisecondCarry + this->elapsed;
        const int64_t whole = total / std::chrono::milliseconds(1);
        this->millisecondCarry = total % std::chrono::milliseconds(1);
        this->elapsedMilliseconds = static_cast<uint32_t>(std::min<int64_t>(whole, UINT32_MAX));
    }

    std::chrono::microseconds System::ElapsedGet() const
    {
        return this->updated ? this->elapsed : this->Timing.GetInterval();
    }

    double System::ElapsedSecondsGet() const
    {
        return static_cast<double>(this->ElapsedGet().count()) / 1e6;
    }

    void System::ClockSet(const ecs::Clock *source)
    {
        this->clock = source != nullptr ? source : &ecs::SteadyClock::Instance();
        const std::chrono::microseconds now = this->clock->Now();
        this->Timing.Restart(now);
        for(auto &timer : this->timers)
        {
            timer.start(now);
        }
        for(auto &timer : this->timersAdded)
        {
            timer.start(now);
        }
        this->updated = false;
        this->previousUpdate = std::chrono::microseconds(0);
        this->elapsed = std::chrono::microseconds(0);
        this->millisecondCarry = std::chrono::microseconds(0);
        this->elapsedMilliseconds = 0;
    }

    uint32_t System::DeltaTimeGet()
    {
        if(!this->updated)
        {
            const int64_t whole = this->Timing.GetInterval() / std::chrono::milliseconds(1);
            return static_cast<uint32_t>(std::min<int64_t>(whole, UINT32_MAX));
        }
        return this->elapsedMilliseconds;
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
        if(!timer.valid())
        {
            throw std::runtime_error("ecs::System(\"" + this->Handle + "\")::TimerAdd(): timer \"" + timer.Name +
                                     "\" interval " + std::to_string(timer.length.count()) +
                                     " us is outside 0 to " + std::to_string(ecs::MAX_INTERVAL.count()) + " us.");
        }
        timer.start(this->clock->Now());
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
