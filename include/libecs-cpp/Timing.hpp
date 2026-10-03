#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace ecs
{
    static_assert(sizeof(std::chrono::microseconds::rep) >= 8, "intervals need a 64-bit microsecond count");

    /*! The longest interval or timer length that is accepted: 100 years of 365.25 days. */
    constexpr std::chrono::microseconds MAX_INTERVAL = std::chrono::hours(24 * 36525);

    /*! The interval of a new schedule: 33 333 microseconds, about 30 updates a second. */
    constexpr std::chrono::microseconds DEFAULT_INTERVAL{1000000 / 30};

    /*! Maximum number of intervals to catch up after a stall. Beyond this, the schedule snaps forward to
     *  avoid a burst of updates. */
    constexpr uint32_t MAX_CATCHUP = 2;

    /*! A repeating schedule: says when the next update is due.
     *
     * The interval is the time between updates, in microseconds. A larger interval means updates happen
     * less often. An interval of zero means due on every call. The longest interval is MAX_INTERVAL;
     * anything below zero or above it is rejected with std::runtime_error.
     *
     * A new schedule is first due one full interval after it was created (or after Restart()). When a
     * call finds that one or more whole intervals have passed, the schedule moves forward by exactly
     * those whole intervals, so the average rate stays exact however unevenly it is asked. If more than
     * MAX_CATCHUP whole intervals have passed, which happens after a long stall, it fires once and
     * starts counting again from that moment instead of catching up.
     *
     * World thread only.
     */
    class Timing
    {
      public:
        Timing(std::chrono::microseconds interval = DEFAULT_INTERVAL)
          : updateInterval(Timing::checked(interval)),
            lastUpdateTime(Timing::steadyNow())
        {
        }

        /*! The old form: the number is the interval in microseconds. Deprecated; it stays for at least the
         *  next minor release and removal is not scheduled in this one. */
        [[deprecated("use Timing(std::chrono::microseconds) instead")]]
        Timing(uint32_t frequency)
          : updateInterval(static_cast<int64_t>(frequency)),
            lastUpdateTime(Timing::steadyNow())
        {
        }

        /*! True when an update is due at the given instant, and then moves the schedule on. The instant
         *  comes from the clock the caller uses; an instant earlier than the last one counts as no time
         *  having passed. An interval of zero is always due and leaves the schedule alone. */
        bool ShouldUpdate(std::chrono::microseconds now)
        {
            if(this->updateInterval == 0)
                return true;

            int64_t current_time = now.count();
            int64_t elapsed = current_time - this->lastUpdateTime;
            if(elapsed < 0)
                elapsed = 0;

            if(elapsed >= this->updateInterval)
            {
                // Remainder-carry: advance by the largest multiple of the interval that fits, so the
                // average rate is exact and drift-free under jittered sampling.
                int64_t intervals = elapsed / this->updateInterval;

                // MAX_CATCHUP clamp: if stalled for too long, snap forward instead of catching up
                // (prevents a burst after suspend or a long pause).
                if(intervals > static_cast<int64_t>(MAX_CATCHUP))
                    this->lastUpdateTime = current_time;
                else
                    this->lastUpdateTime += intervals * this->updateInterval;

                return true;
            }
            return false;
        }

        /*! As above, reading the real steady clock. */
        bool ShouldUpdate()
        {
            return this->ShouldUpdate(std::chrono::microseconds(Timing::steadyNow()));
        }

        /*! Starts a new schedule at the given instant: the first update is due one full interval later. */
        void Restart(std::chrono::microseconds now)
        {
            this->lastUpdateTime = now.count();
        }

        /*! Changes the interval. The next due time carries on from the last one, so nothing is skipped and
         *  nothing bursts. Throws std::runtime_error, changing nothing, outside 0 to MAX_INTERVAL. */
        void SetInterval(std::chrono::microseconds interval)
        {
            this->updateInterval = Timing::checked(interval);
        }

        /*! The interval between updates. */
        std::chrono::microseconds GetInterval() const
        {
            return std::chrono::microseconds(this->updateInterval);
        }

        /*! The old name of SetInterval(): the number is the interval in microseconds. Deprecated; it stays
         *  for at least the next minor release and removal is not scheduled in this one. */
        [[deprecated("use SetInterval(std::chrono::microseconds) instead")]]
        void SetFrequency(uint32_t frequency)
        {
            this->updateInterval = static_cast<int64_t>(frequency);
        }

        /*! The old name of GetInterval(): the interval in microseconds, or 4 294 967 295 if it is longer
         *  than that number can hold. Use GetInterval(). Deprecated; it stays for at least the next minor
         *  release and removal is not scheduled in this one. */
        [[deprecated("use GetInterval() instead")]]
        uint32_t GetFrequency() const
        {
            return static_cast<uint32_t>(std::min<int64_t>(this->updateInterval, UINT32_MAX));
        }

      private:
        int64_t updateInterval;
        int64_t lastUpdateTime;

        static int64_t checked(std::chrono::microseconds interval)
        {
            if(interval.count() < 0 || interval > MAX_INTERVAL)
            {
                throw std::runtime_error("ecs::Timing: interval " + std::to_string(interval.count()) +
                                         " us is outside 0 to " + std::to_string(MAX_INTERVAL.count()) + " us.");
            }
            return interval.count();
        }

        static int64_t steadyNow()
        {
            using namespace std::chrono;
            return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
        }
    };
}
