#pragma once

#include <chrono>

namespace ecs 
{
    class Timing
    {
      public:
        Timing(uint32_t frequency = 1000000 / 30) // Default frequency: 30 updates per second
          : updateFrequency(frequency),
            lastUpdateTime(getCurrentMicroseconds()) {}

        bool ShouldUpdate()
        {
            uint64_t current_time = getCurrentMicroseconds();
            if(current_time - this->lastUpdateTime >= this->updateFrequency)
            {
                this->lastUpdateTime = current_time;
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
