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
     * A bare number in place of a duration means seconds. That form is deprecated.
     */
    class Timer
    {
      public:
        /*! Creates a timer from a name, a callback, a length and whether it repeats. The length is
         *  checked when the timer is added with System::TimerAdd(). */
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

        /*! Creates a timer whose length is a bare number of seconds. Deprecated since 1.8.0: pass a
         *  std::chrono duration instead. A negative number or one above the maximum is kept as an
         *  out-of-range length, which System::TimerAdd() rejects; it never wraps around. */
        template<std::integral T>
        [[deprecated("pass a std::chrono duration such as std::chrono::seconds(n) instead of a bare number")]]
        Timer(std::string name,
          std::function<void()> callback,
          T seconds,
          bool repeat = true)
          : Timer(std::move(name), std::move(callback), Timer::fromSeconds(seconds), repeat)
        {
        }

        /*! Runs the callback if the timer is due by the real steady clock, and says whether it ran. This
         *  is for a timer that is not owned by a system. A timer added to a system is driven by the system,
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
        /*! The name that System::TimerClear() cancels the timer by. Several timers may share a name. */
        std::string Name;
        /*! True for a timer that fires every length, false for one that fires once. */
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

    /*! \cond INTERNAL */
    /* Hand-off point for messages sent to one system. Internal: use System::MessageSubmit().
     *
     * Any thread appends to pending under lock. The container thread moves pending into the system's
     * message queue. count mirrors pending.size() so the empty case needs no lock.
     */
    struct Mailbox
    {
        std::mutex lock;
        std::vector<nlohmann::json> pending;
        std::atomic<std::size_t> count{0};
    };
    /*! \endcond */

    /*! Base class for the logic that runs in a container.
     *
     * A subclass overrides Update() with its work and Export() with a description of itself, and may
     * override Initialize(), Shutdown() and Configure(). On each pass in which the system's Timing says
     * an update is due, the container calls UpdateSystem(), which fires the timers that are due and then
     * calls Update().
     *
     * \par Threading
     * MessageSubmit() and Handle may be used from any thread. Every other member is for the container
     * thread only (see Container). A system must be alive when MessageSubmit() is called on it directly:
     * sending through Container::MessageSubmit() or Manager::MessageSubmit() is safe against the system
     * being removed, a direct pointer to the system is not.
     *
     * \par Messages
     * A message sent to the system is moved into messages at the start of the system's next
     * UpdateSystem(), or earlier if MessagesWaiting() is called. Read the queue in Update():
     *
     *     while(!this->messages.empty())
     *     {
     *         nlohmann::json message = this->messages.front();
     *         this->messages.pop();
     *         // use the message
     *     }
     *
     * \par
     * The mailbox is unbounded. An application that needs back-pressure provides it.
     */
    class System
    {
      public:
        /*! Creates a system whose handle is a newly generated UUID. */
        System();
        /*! Creates a system with the given handle. The handle is fixed for the life of the object and
         *  cannot be assigned afterwards.
         *
         * A subclass names itself in its constructor's initialiser list:
         *
         *     class Foo : public ecs::System
         *     {
         *       public:
         *         Foo() : ecs::System("Foo") {}
         *     };
         *
         * A plugin factory that returns a new Foo reaches the same constructor. An empty handle is
         * refused when the system is registered. */
        System(const std::string &handle);
        /*! Destroys the system. Container->Log() works from a subclass's destructor. */
        virtual ~System() = default;
        /*! Called once before the system's first Update(); override it to set the system up.
         *
         * It is called exactly once per system, whichever way the system arrived: registered before the
         * container started, registered later from code on the container thread (another system's
         * Initialize() or Update(), a timer callback or a deferred function), registered in a container
         * that the application drives with Update(), or registered as a replacement. Systems that are
         * started together are started in registration order. A system removed before it was reached is
         * never started.
         *
         * If Initialize() throws, the error is logged with the system's handle and rethrown. The system
         * counts as started: it is not started again, it is updated, and it receives Shutdown() when it
         * leaves the container. The systems registered after it are started at the start of the next pass.
         *
         * Container->Log() works from here.
         *
         * Thread: container thread only. */
        virtual void Initialize() {};
        /*! Called once before a started system is destroyed; override it to release what the system holds.
         *
         * It is called exactly once for every system that was started, and never for one that was not.
         * The system receives no update and no routed message afterwards. When a container stops or is
         * destroyed, its systems are shut down last registered first.
         *
         * The thread depends on how the system leaves:
         * - Removed or replaced: the container thread, at the end of the pass that removed it (at once
         *   when removed outside a pass).
         * - A container with its own thread stops, or its manager shuts down: that thread, before it ends.
         * - A container driven by the application's Update() calls is stopped or destroyed: the thread
         *   that calls Container::Stop() or destroys the container.
         *
         * An exception thrown from Shutdown() is logged at level "error" with the system's handle and
         * swallowed on every path; the other systems still receive Shutdown() and are released. A
         * Shutdown() that blocks is waited for, not abandoned.
         *
         * Container->Log() works from here on every path. */
        virtual void Shutdown() {};
        /*! Receives the system's configuration; override it to read settings. The base implementation
         *  does nothing, and the library does not call it: the code that creates the system does.
         *
         * Thread: container thread only. */
        virtual void Configure(const nlohmann::json &config);
        /*! Called on each pass in which the system is due; override it with the system's work.
         *
         * Thread: container thread only. */
        virtual void Update() {};
        /*! Fires the timers that are due, then calls Update(). The container calls this on each pass in
         *  which the system is due.
         *
         * The clock is read once, at the start, and that one reading decides which timers are due and
         * sets the elapsed time of this update (see ElapsedGet()).
         *
         * Timers are visited in the order they were added. A timer added by a callback does not fire in
         * this call. A timer cleared by a callback and not yet reached does not fire. Afterwards exactly
         * the one-shot timers that fired are removed, and the timer changes made by callbacks take effect
         * before Update() runs. If a callback removes its own system, no further timers fire and Update()
         * is not called. If a callback throws, the error is rethrown after the timer changes have
         * completed. When the call is made by Container::Update(), the container also logs the error with
         * the system's handle.
         *
         * Thread: container thread only. */
        void UpdateSystem();
        /*! The system's handle, given to the constructor and never changed.
         *
         * Thread: any. */
        const std::string Handle;
        /*! The container the system is registered in, or null before registration. Set by
         *  Container::System() only.
         *
         * Thread: container thread only. */
        ecs::Container *Container = nullptr;
        /*! Delivers a message to this system.
         *
         * Returns without waiting for the system's update. The message is moved into messages at the
         * start of the system's next UpdateSystem(), which may be later in the pass that is running.
         * Messages from one sender arrive in the order sent, each exactly once.
         *
         * Thread: any. The system must be alive. */
        void MessageSubmit(const nlohmann::json &message);
        /*! Describes the system as JSON; every subclass implements it. It is a const query and must not
         *  add or remove systems, entities or components.
         *
         * Thread: container thread only. */
        virtual nlohmann::json Export() const = 0;
        /*! The components of the system's container (Container::Components), or null before
         *  registration.
         *
         * Thread: container thread only. */
        ecs::TypeEntityComponentList *Components = nullptr;
        /*! The schedule that decides on which passes the system is updated. Change the interval with
         *  Timing.SetInterval(); the default is ecs::DEFAULT_INTERVAL, about 30 updates a second.
         *
         * Thread: container thread only. */
        ecs::Timing Timing;
        /*! Moves any delivered messages into messages, then returns how many messages are waiting to be
         *  read. A message delivered before the call is counted.
         *
         * Thread: container thread only. */
        size_t MessagesWaiting();
        /*! Returns the time that passed between the previous update of this system and the current one.
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
         * reports zero for its first update.
         *
         * Thread: container thread only. */
        std::chrono::microseconds ElapsedGet() const;
        /*! Returns ElapsedGet() in seconds.
         *
         * Thread: container thread only. */
        double ElapsedSecondsGet() const;
        /*! Replaces the clock this system reads.
         *
         * The schedule, the timers and the elapsed-time measurement start again from the new clock's
         * time, because readings of different clocks cannot be compared. The pointer is not owned and the
         * clock must outlive the system. A null pointer selects the real steady clock, which is the
         * default. A container sets the clock of every system it holds with Container::ClockSet().
         *
         * Thread: container thread only. */
        void ClockSet(const ecs::Clock *clock);
        /*! Returns the whole milliseconds of this update's elapsed time. Deprecated since 1.8.0: use
         *  ElapsedGet() or ElapsedSecondsGet().
         *
         * The part of a millisecond that is left over is carried into the next update, so the running
         * total stays within a millisecond of the true total. The value is the same on every call during
         * one update. It is at most 4 294 967 295, which a single update longer than about 49.7 days
         * reaches; ElapsedGet() is never limited.
         *
         * Thread: container thread only. */
        [[deprecated("use ElapsedGet() or ElapsedSecondsGet() instead")]]
        uint32_t DeltaTimeGet();
        /*! Cancels every timer with this name.
         *
         * Safe to call from a timer callback, including for the callback's own name. Cancelling and then
         * adding the same name leaves only the new timer; adding and then cancelling removes both. From
         * Update() the change takes effect at once.
         *
         * Thread: container thread only. */
        void TimerClear(const std::string &name);
        /*! Adds a timer, which first fires one full length from now.
         *
         * Throws std::runtime_error, adding nothing, when the length is below zero or above
         * ecs::MAX_INTERVAL. Safe to call from a timer callback, on this system or on any other system of
         * the same container. A timer added by one of this system's own timer callbacks is considered
         * from the system's next update. From Update() the timer is added at once.
         *
         * Thread: container thread only. */
        void TimerAdd(Timer timer);
        /*! Sends a line to the container's log destination with this system's handle as a prefix.
         *
         * Before the system is registered the line is held, and registering the system delivers every
         * held line to the destination, in order. Held lines are not capped. The usual levels are
         * "error", "warning", "info" and "debug"; any other name is passed on as given.
         *
         * Thread: container thread only. */
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
        /*! Sends the lines held from before attachment to the container's log destination, once and in order,
         *  each with the handle prefix. A destination that throws does not stop the remaining lines.
         *  Does nothing when no container is attached or nothing is held. Takes no lock. */
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
        /*! Messages ready to read, oldest first. Read them in Update() with front() and pop().
         *
         * Thread: container thread only. */
        std::queue<nlohmann::json> messages;
        /*! The construction time. Deprecated since 1.8.0 and not maintained by the library: use
         *  ElapsedGet(). */
        [[deprecated("use ElapsedGet() or ElapsedSecondsGet() instead")]]
        std::chrono::steady_clock::time_point lastTime = std::chrono::steady_clock::now();
        /*! Components a subclass has marked for removal, as entity handles by type name. A subclass fills
         *  it while it iterates Components and then calls componentsClear(). */
        std::unordered_map<std::string, std::vector<std::string>> componentsToDelete;
        /*! Removes every component listed in componentsToDelete from the container and empties the list.
         *  Before the system is registered it logs a warning and does nothing. */
        void componentsClear();
        /*! The system's timers. Change them with TimerAdd() and TimerClear() only. */
        std::vector<ecs::Timer> timers;
        /*! Lines logged before the system was registered, as (message, level) pairs in the order they
         *  were logged. Container::System() delivers them. They are not capped: a system that logs
         *  without limit before it is registered holds them all in memory. */
        std::vector<std::pair<std::string, std::string>> bufferedLogMessages;
    };
}
