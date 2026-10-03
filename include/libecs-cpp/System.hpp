#pragma once

#include <string>
#include <queue>
#include <functional>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <libecs-cpp/Timing.hpp>

namespace ecs
{
    class Container;
    class System;

    /*! A callback that fires after an interval, owned by a System.
     *
     * A repeating timer (the default) fires every interval until it is cleared. A one-shot timer fires
     * once and is then discarded; if its callback adds a new timer under the same name, the new timer is
     * kept.
     */
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
        friend class ecs::System;
        bool discarded = false;
        bool due()
        {
            return this->timing.ShouldUpdate() && this->callback;
        }
        void fire()
        {
            this->callback();
        }
        ecs::Timing timing;
        std::function<void()> callback = nullptr;
    };

    /*! Hand-off point for messages sent to one system. Internal: use System::MessageSubmit().
     *
     * Any thread appends to pending under lock. The world thread moves pending into the system's
     * message queue. count mirrors pending.size() so the empty case needs no lock.
     */
    struct Mailbox
    {
        std::mutex lock;
        std::vector<nlohmann::json> pending;
        std::atomic<std::size_t> count{0};
    };

    class System
    {
      public:
        System();
        System(const std::string &handle);
        virtual ~System() = default;
        virtual void Initialize() {};
        virtual void Shutdown() {};
        virtual void Configure(const nlohmann::json &config);
        virtual void Update() {};
        /*! Fires the timers that are due, then calls Update().
         *
         * The timer walk visits timers in the order they were added. A timer added during the walk does
         * not fire in it. A timer cleared during the walk and not yet reached does not fire. After the
         * walk, exactly the one-shot timers that fired are removed, and the timer changes made by
         * callbacks take effect before Update() runs. If a callback removes its own system, no further
         * timers fire and Update() is not called. If a callback throws, the error is rethrown after the timer
         * changes have completed. When the pass is run by Container::Update(), the container also logs it
         * with the system's identifier.
         */
        void UpdateSystem();
        std::string Handle;
        ecs::Container *Container = nullptr;
        /*! Delivers a message to this system. Safe to call from any thread at any time. Returns without
         *  waiting for the system's update; the system sees the message in a later update of its world.
         *  Messages from one sender arrive in the order sent, each exactly once. */
        void MessageSubmit(const nlohmann::json &message);
        virtual nlohmann::json Export() const = 0;
        ecs::TypeEntityComponentList *Components = nullptr;
        ecs::Timing Timing;
        /*! Counts delivered messages that have not been read, including ones not yet moved to the
         *  message queue. Call from the world thread only. */
        size_t MessagesWaiting();
        uint32_t DeltaTimeGet();
        /*! Cancels every timer with this name. Safe to call from a timer callback, including for the
         *  callback's own name. Cancelling and then adding the same name leaves only the new timer; adding
         *  and then cancelling removes both. From Update() the change takes effect at once. */
        void TimerClear(const std::string &name);
        /*! Adds a timer. Safe to call from a timer callback, on this system or on any other system of the
         *  same world. A timer added during this system's timer walk is considered from its next update.
         *  From Update() the timer is added at once. */
        void TimerAdd(Timer timer);
        void Log(const std::string &message, const std::string &level);
      private:
        friend class ecs::Container;
        std::shared_ptr<ecs::Mailbox> mailbox = std::make_shared<ecs::Mailbox>();
        std::vector<nlohmann::json> staging;
        void mailboxDrain();
        uint32_t timerWalkDepth = 0;
        bool timersDiscarded = false;
        std::vector<ecs::Timer> timersAdded;
        bool removed = false;
        void timerWalkFinish();
      protected:
        /*! Messages ready to read. Touched by the world thread only. */
        std::queue<nlohmann::json> messages;
        std::chrono::steady_clock::time_point lastTime = std::chrono::steady_clock::now();
        std::unordered_map<std::string, std::vector<std::string>> componentsToDelete;
        void componentsClear();
        std::vector<ecs::Timer> timers;
        std::vector<std::pair<std::string, std::string>> bufferedLogMessages;
    };
}
