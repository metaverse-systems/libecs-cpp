#include <chrono>

namespace ecs 
{
    class Timing
    {
      public:
        Timing(uint32_t frequency = 1000000 / 30) // Default frequency: 30 updates per second
          : update_frequency(frequency),
            last_update_time(GetCurrentMicroseconds()) {}

        bool ShouldUpdate()
        {
            uint64_t current_time = GetCurrentMicroseconds();
            if(current_time - this->last_update_time >= this->update_frequency)
            {
                this->last_update_time = current_time;
                return true;
            }
            return false;
        }

        void SetFrequency(uint32_t frequency) {
            this->update_frequency = frequency;
        }

        uint32_t GetFrequency() const {
            return this->update_frequency;
        }

      private:
        uint32_t update_frequency;
        uint64_t last_update_time;

        static uint64_t GetCurrentMicroseconds() {
            using namespace std::chrono;
            return duration_cast<microseconds>(
                steady_clock::now().time_since_epoch()
            ).count();
        }
    };
}
