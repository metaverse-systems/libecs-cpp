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
        System(const std::string &handle);
        virtual ~System() = default;
        virtual void Initialize();
        virtual void Configure(const nlohmann::json &config);
        virtual void Update() {};
        void UpdateSystem();
        std::string Handle;
        ecs::Container *Container = nullptr;
        void MessageSubmit(const nlohmann::json &message);
        virtual nlohmann::json Export() const = 0;
        ecs::TypeEntityComponentList *Components = nullptr;
        ecs::Timing Timing;
        size_t MessagesWaiting();
        uint32_t DeltaTimeGet();
        void TimerClear(const std::string &name);
        void TimerAdd(Timer timer);
        void Log(const std::string &message, const std::string &level);
      protected:
        std::queue<nlohmann::json> messages;
        std::chrono::steady_clock::time_point lastTime = std::chrono::steady_clock::now();
        std::unordered_map<std::string, std::vector<std::string>> componentsToDelete;
        void componentsClear();
        std::vector<ecs::Timer> timers;
    };
}
