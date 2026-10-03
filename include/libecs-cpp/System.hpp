#pragma once

#include <string>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <queue>
#include <functional>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <chrono>
#include <concepts>
#include <type_traits>
#include <libecs-cpp/json.hpp>
#include <libecs-cpp/Component.hpp>
#include <libecs-cpp/Clock.hpp>
#include <libecs-cpp/Timing.hpp>

namespace ecs
{
    class Container;
    class System;

    /*! A callback that fires after a length of time, owned by a System.
     *
     * A repeating timer (the default) fires every length until it is cleared. A one-shot timer fires
     * once and is then discarded; if its callback adds a new timer under the same name, the new timer is
     * kept.
     *
     * The length is a std::chrono duration, for example std::chrono::hours(2), from zero up to
     * ecs::MAX_INTERVAL. A timer first fires one full length after it is added with System::TimerAdd(),
     * and not before. A length of zero fires once on every update of the system. A length outside the
     * range is rejected by TimerAdd() with std::runtime_error.
     *
     * A bare number in place of a duration is still accepted and means seconds.
     */
    class Timer
    {
      public:
        Timer(std::string name,
          std::function<void()> callback,
          std::chrono::microseconds interval = std::chrono::seconds(30),
          bool repeat = true)
          : Repeat(repeat), length(interval)
        {
            this->Name = name;
            this->callback = callback;
            if(interval.count() >= 0 && interval <= MAX_INTERVAL)
            {
                this->timing.SetInterval(interval);
            }
        }

        /*! The old form: a bare number of seconds. A negative number or one above the maximum is kept as an
         *  out-of-range length, which System::TimerAdd() rejects; it never wraps around.
         *  Deprecated: pass a std::chrono duration instead. It stays for at least the next minor release and
         *  removal is not scheduled in this one. */
        template<std::integral T>
        [[deprecated("pass a std::chrono duration such as std::chrono::seconds(n) instead of a bare number")]]
        Timer(std::string name,
          std::function<void()> callback,
          T seconds,
          bool repeat = true)
          : Timer(std::move(name), std::move(callback), Timer::fromSeconds(seconds), repeat)
        {
        }

        /*! Runs the callback if the timer is due by the real steady clock. This is the stand-alone path
         *  for a timer that is not owned by a system; a timer added to a system is driven by the system,
         *  with the system's clock, and must not be run through this. */
        bool CallbackRun()
        {
            if(this->valid() && this->timing.ShouldUpdate() && this->callback)
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
        std::chrono::microseconds length;

        bool valid() const
        {
            return this->length.count() >= 0 && this->length <= MAX_INTERVAL;
        }
        /*! Starts the schedule at the given instant. Only called with a valid length. */
        void start(std::chrono::microseconds now)
        {
            this->timing.SetInterval(this->length);
            this->timing.Restart(now);
        }
        bool due(std::chrono::microseconds now)
        {
            return this->timing.ShouldUpdate(now) && this->callback;
        }
        void fire()
        {
            this->callback();
        }
        template<typename T>
        static std::chrono::microseconds fromSeconds(T seconds)
        {
            constexpr long long maximum = std::chrono::duration_cast<std::chrono::seconds>(MAX_INTERVAL).count();
            if constexpr(std::is_signed_v<T>)
            {
                const long long value = seconds;
                if(value < 0) return std::chrono::microseconds(-1);
                if(value > maximum) return MAX_INTERVAL + std::chrono::microseconds(1);
                return std::chrono::seconds(value);
            }
            else
            {
                const unsigned long long value = seconds;
                if(value > static_cast<unsigned long long>(maximum)) return MAX_INTERVAL + std::chrono::microseconds(1);
                return std::chrono::seconds(static_cast<long long>(value));
            }
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

    /*! Base class for logic that runs in a world.
     *
     * Threading
     *
     * - Any thread: MessageSubmit(). The system must be alive; routing through Container or Manager is
     *   safe against the system being removed, a direct pointer to the system is not.
     * - World thread only: Initialize(), Configure(), Update(), UpdateSystem(), Export(), Shutdown(),
     *   MessagesWaiting(), TimerAdd(), TimerClear(), ElapsedGet(), ElapsedSecondsGet(), ClockSet(),
     *   DeltaTimeGet(), Log(), the messages queue and the
     *   public members Container, Components and Timing (set during registration). Handle is fixed at
     *   construction and may be read from any thread.
     * - Messages are delivered to the system's mailbox from any thread and become visible in messages
     *   at the start of the system's next UpdateSystem(), or earlier if MessagesWaiting() is called.
     *   Mailboxes are unbounded; an application that
     *   needs back-pressure provides it.
     */
    class System
    {
      public:
        /*! Builds a system whose identifier is a freshly generated UUID. */
        System();
        /*! Builds a system with the given identifier. This is the one way to name a system: the
         *  identifier is fixed for the life of the object and cannot be assigned afterwards.
         *
         * A subclass names itself in its constructor's initialiser list:
         *
         *     class Foo : public ecs::System
         *     {
         *       public:
         *         Foo() : ecs::System("Foo") {}
         *     };
         *
         * A plugin factory that returns a new Foo reaches the same constructor. An empty identifier is
         * refused when the system is registered. */
        System(const std::string &handle);
        virtual ~System() = default;
        /*! Start-up notification. World thread only.
         *
         * Called exactly once per system, before its first Update(), whichever way the system arrived: it
         * was registered before the world started, registered later from any world-thread code
         * (including another system's Initialize() or Update(), or a deferred function), registered in a
         * world that the application drives with Update(), or registered as a replacement. Systems that
         * are started together are started in registration order. A system removed before it was reached
         * is never started.
         *
         * If Initialize() throws, the error is logged with the system's identifier and rethrown once the
         * start-up step has finished its bookkeeping. The system still counts as started: it is not
         * started again, it is updated, and it receives Shutdown() when it leaves the world.
         *
         * Logging with Container->Log() works from here.
         */
        virtual void Initialize() {};
        /*! Shutdown notification. World thread only, in the sense below.
         *
         * Called exactly once for every system that was started, and never for one that was not. It is
         * called before the system is destroyed, and the system receives no update or routed message
         * afterwards. The order is last registered first when a world stops or is destroyed.
         *
         * The thread depends on the path. A system removed or replaced is notified on the world thread, at
         * the end of the walk that removed it (at once when removed outside a walk). When a world with its
         * own thread stops, or its manager shuts down, the notification runs on that thread before it
         * ends. When a world driven by the application's own Update() calls is stopped or destroyed, it
         * runs on the thread that calls Stop() or destroys the world.
         *
         * An exception thrown from Shutdown() is logged at level "error" with the system's identifier and
         * swallowed on every path; the other systems are still notified and released. A Shutdown() that
         * blocks is waited for, not abandoned.
         *
         * Container->Log() works from Shutdown() and from the destructor of the system, on every path.
         */
        virtual void Shutdown() {};
        /*! World thread only. */
        virtual void Configure(const nlohmann::json &config);
        /*! World thread only. */
        virtual void Update() {};
        /*! Fires the timers that are due, then calls Update(). World thread only.
         *
         * The clock is read once, at the start, and that one reading decides which timers are due and
         * sets the elapsed time of this update (see ElapsedGet()).
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
        /*! The system's identifier, given to the constructor and never changed. */
        const std::string Handle;
        /*! World thread only. Set during registration. */
        ecs::Container *Container = nullptr;
        /*! Delivers a message to this system. Safe to call from any thread at any time. Returns without
         *  waiting for the system's update; the message is moved into messages at the start of the
         *  system's next UpdateSystem(), which may be later in the pass that is running. Messages from
         *  one sender arrive in the order sent, each exactly once. */
        void MessageSubmit(const nlohmann::json &message);
        /*! World thread only. */
        virtual nlohmann::json Export() const = 0;
        /*! World thread only. Set during registration. */
        ecs::TypeEntityComponentList *Components = nullptr;
        ecs::Timing Timing;
        /*! Moves any delivered messages into the message queue, then returns how many messages are
         *  waiting to be read, so a message delivered before the call is counted. Call from the world
         *  thread only. */
        size_t MessagesWaiting();
        /*! The time that passed between the previous update of this system and the current one, in
         *  microseconds. World thread only.
         *
         * It is measured once per update, from the same clock reading that decided the system was due, and
         * it is the same on every call during that update, from Update(), a timer callback or any helper.
         * Reading it changes nothing, and a system that never reads it loses no time. The values of
         * consecutive updates add up to the time the clock moved. Nothing is clamped: after a long stall
         * the whole stall is reported, so code that integrates over the step should cap it itself, for
         * example with std::min(this->ElapsedSecondsGet(), 0.25).
         *
         * Before the first update, and during it, the value is the system's configured interval (Timing),
         * because there is no earlier update to measure from. A system whose interval is zero therefore
         * reports zero for its first update. */
        std::chrono::microseconds ElapsedGet() const;
        /*! ElapsedGet() in seconds. World thread only. */
        double ElapsedSecondsGet() const;
        /*! Replaces the clock this system reads. World thread only.
         *
         * The schedule, the timers and the elapsed-time measurement start again from the new clock's
         * time, because readings of different clocks cannot be compared. The pointer is not owned and the
         * clock must outlive the system. A null pointer selects the real steady clock, which is the
         * default. A world sets the clock of every system it holds with Container::ClockSet(). */
        void ClockSet(const ecs::Clock *clock);
        /*! The whole milliseconds of this update's elapsed time, with the part of a millisecond that is
         *  left over carried into the next update, so the running total stays within a millisecond of the
         *  true total. The same on every call during one update. At most 4 294 967 295, which a single
         *  update longer than about 49.7 days reaches; ElapsedGet() is never limited. World thread only.
         *  Deprecated: use
         *  ElapsedGet() or ElapsedSecondsGet(). It stays for at least the next minor release and removal is
         *  not scheduled in this one. */
        [[deprecated("use ElapsedGet() or ElapsedSecondsGet() instead")]]
        uint32_t DeltaTimeGet();
        /*! Cancels every timer with this name. World thread only. Safe to call from a timer callback, including for the
         *  callback's own name. Cancelling and then adding the same name leaves only the new timer; adding
         *  and then cancelling removes both. From Update() the change takes effect at once. */
        void TimerClear(const std::string &name);
        /*! Adds a timer, which first fires one full length from now. Throws std::runtime_error, adding
         *  nothing, when the length is below zero or above ecs::MAX_INTERVAL. World thread only. Safe to call from a timer callback, on this system or on any other system of the
         *  same world. A timer added during this system's timer walk is considered from its next update.
         *  From Update() the timer is added at once. */
        void TimerAdd(Timer timer);
        /*! Sends a message to the world's log destination with this system's identifier as a prefix.
         *  Before the system is attached to a world the message is held, and registering the system
         *  delivers every held message to the destination, in order. Held messages are not capped.
         *  A message with no severity is "info", as on the world's own Log(). The usual severity names are
         *  "error", "warning", "info" and "debug"; any other name is passed on as given. World thread only. */
        void Log(const std::string &message, const std::string &level = "info");
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
        /*! Sends the lines held from before attachment to the world's log destination, once and in order,
         *  each with the identifier prefix. A destination that throws does not stop the remaining lines.
         *  Does nothing when no world is attached or nothing is held. Takes no lock. */
        void bufferedDeliver();
        /*! One update at the given clock reading: measures the elapsed time, then runs the timers and
         *  Update(). */
        void updateSystem(std::chrono::microseconds now);
        void elapsedMeasure(std::chrono::microseconds now);
        const ecs::Clock *clock = &ecs::SteadyClock::Instance();
        bool updated = false;
        std::chrono::microseconds previousUpdate{0};
        std::chrono::microseconds elapsed{0};
        std::chrono::microseconds millisecondCarry{0};
        uint32_t elapsedMilliseconds = 0;
      protected:
        /*! Messages ready to read. World thread only. */
        std::queue<nlohmann::json> messages;
        /*! Deprecated and no longer maintained by the library: use ElapsedGet() instead. */
        [[deprecated("use ElapsedGet() or ElapsedSecondsGet() instead")]]
        std::chrono::steady_clock::time_point lastTime = std::chrono::steady_clock::now();
        std::unordered_map<std::string, std::vector<std::string>> componentsToDelete;
        void componentsClear();
        std::vector<ecs::Timer> timers;
        /*! Lines logged before the system was attached to a world, as (message, severity) pairs in the
         *  order they were logged. The world delivers them once, in order, with this system's identifier
         *  as a prefix, as the last step of registering the system. They are not capped: a system that
         *  logs without limit before it is registered holds them all in memory. */
        std::vector<std::pair<std::string, std::string>> bufferedLogMessages;
    };
}
