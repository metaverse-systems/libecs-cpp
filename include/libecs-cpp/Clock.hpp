#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace ecs
{
    /*! A source of time for systems and worlds.
     *
     * Now() is the time since some fixed starting point that the clock chooses. It never decreases. Only
     * differences between two readings of the same clock mean anything.
     *
     * A system or a world holds a pointer to its clock and does not own it, so the clock must outlive
     * whatever uses it. The default is the real steady clock (SteadyClock). A ManualClock lets a test
     * move time by exact amounts without waiting.
     */
    class Clock
    {
      public:
        virtual ~Clock() = default;
        /*! Time since the clock's starting point, in microseconds. Never decreases. */
        virtual std::chrono::microseconds Now() const = 0;
    };

    /*! The real clock: std::chrono::steady_clock, truncated to whole microseconds. Stateless and safe to
     *  use from any thread. */
    class SteadyClock : public Clock
    {
      public:
        std::chrono::microseconds Now() const override
        {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch());
        }

        /*! The shared instance, which lives until the process ends. Does not allocate. */
        static const SteadyClock &Instance()
        {
            static const SteadyClock instance;
            return instance;
        }
    };

    /*! A clock that only moves when told to, for tests. Safe to read and move from any thread. A world that
     *  has its own thread still sleeps on the real clock between passes, so use a ManualClock with worlds
     *  that are driven by calls to Container::Update(). */
    class ManualClock : public Clock
    {
      public:
        explicit ManualClock(std::chrono::microseconds start = std::chrono::microseconds(0))
          : count(start.count())
        {
        }

        std::chrono::microseconds Now() const override
        {
            return std::chrono::microseconds(this->count.load());
        }

        /*! Moves time forward. A negative amount throws std::runtime_error and changes nothing. */
        void Advance(std::chrono::microseconds delta)
        {
            if(delta.count() < 0)
            {
                throw std::runtime_error("ecs::ManualClock::Advance(): a clock cannot move backwards (" +
                                         std::to_string(delta.count()) + " us).");
            }
            this->count.fetch_add(delta.count());
        }

        /*! Sets the time. The caller keeps it from going backwards. */
        void Set(std::chrono::microseconds value)
        {
            this->count.store(value.count());
        }

      private:
        std::atomic<int64_t> count;
    };
}
