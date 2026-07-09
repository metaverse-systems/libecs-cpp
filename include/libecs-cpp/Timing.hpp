#pragma once

#include <chrono>
#include <algorithm>

namespace ecs
{
    // Maximum number of intervals to catch up after a stall.
    // Beyond this, snap forward to avoid burst execution.
    constexpr uint32_t MAX_CATCHUP = 2;

    class Timing
    {
      public:
        Timing(uint32_t frequency = 1000000 / 30) // Default frequency: 30 updates per second
          : updateFrequency(frequency),
            lastUpdateTime(getCurrentMicroseconds()) {}

        bool ShouldUpdate()
        {
            // Frequency 0 means "always fire" (test fixture path).
            if (this->updateFrequency == 0)
                return true;

            uint64_t current_time = getCurrentMicroseconds();
            uint64_t elapsed = current_time - this->lastUpdateTime;

            if (elapsed >= this->updateFrequency)
            {
                // Remainder-carry: advance by the largest multiple of
                // updateFrequency that fits, so the average rate is exact
                // and drift-free under jittered sampling.
                uint64_t intervals = elapsed / this->updateFrequency;

                // MAX_CATCHUP clamp: if stalled for too long, snap forward
                // instead of catching up (prevents burst after suspend/GC).
                if (intervals > MAX_CATCHUP)
                    this->lastUpdateTime = current_time;
                else
                    this->lastUpdateTime += intervals * this->updateFrequency;

                return true;
            }
            return false;
        }

        void SetFrequency(uint32_t frequency) {
            this->updateFrequency = frequency;
        }

        uint32_t GetFrequency() const {
            return this->updateFrequency;
        }

      private:
        uint32_t updateFrequency;
        uint64_t lastUpdateTime;

        static uint64_t getCurrentMicroseconds() {
            using namespace std::chrono;
            return duration_cast<microseconds>(
                steady_clock::now().time_since_epoch()
            ).count();
        }
    };
}
