#pragma once

#include <string>
#include <queue>
#include <functional>
#include <libecs-cpp/Timing.hpp>

namespace ecs
{
    class Container;

    class Timer
    {
      public:
        Timer(std::string name,
          std::function<void()> callback,
          uint32_t interval = 30 /* seconds */,
          bool repeat = true)
          : Repeat(repeat)
        {
            this->Name = name;
            this->callback = callback;
            this->timing.SetFrequency(1000000 * interval); // Convert seconds to microseconds
        }

        bool CallbackRun()
        {
            if (this->timing.ShouldUpdate() && this->callback)
            {
                this->callback();
                return true;
            }
            return false;
        }
        std::string Name;
        bool Repeat;

      private:
        ecs::Timing timing;
        std::function<void()> callback = nullptr;
    };

    class System
    {
      public:
        System();
        System(std::string Handle);
        virtual void Initialize();
        virtual void Configure(nlohmann::json config);
        virtual void Update() {};
        void UpdateSystem();
        std::string Handle;
        ecs::Container *Container = nullptr;
        void MessageSubmit(nlohmann::json);
        virtual nlohmann::json Export() = 0;
        ecs::TypeEntityComponentList *Components = nullptr;
        ecs::Timing Timing;
        const size_t MessagesWaiting();
        const uint32_t DeltaTimeGet();
        void TimerClear(std::string name);
        void TimerAdd(Timer timer);
      protected:
        std::queue<nlohmann::json> messages;
        std::chrono::steady_clock::time_point LastTime = std::chrono::steady_clock::now();
        std::unordered_map<std::string, std::vector<std::string>> ComponentsToDelete;
        void ComponentsClear();
        std::vector<ecs::Timer> Timers;
    };
}
