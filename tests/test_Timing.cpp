#include <catch2/catch_all.hpp>
#include <libecs-cpp/Timing.hpp>
#include <thread>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

using namespace ecs;

// With a 109850 us interval sampled on a 30 Hz grid (33333 us), each fire must
// advance the schedule by whole intervals rather than reset it to the current
// time. Sampling for about two seconds against absolute deadlines, the number
// of fires must match the number of whole intervals that really elapsed (within
// one). Losing the remainder on each fire would make the count about 17% low.
TEST_CASE("Timing remainder-carry keeps drift-free average", "[timing][T1b]")
{
    constexpr uint32_t freq = 109850;
    constexpr int64_t step = 33333;
    constexpr int steps = 60;
    Timing timing(freq);

    auto start = std::chrono::steady_clock::now();
    int fires = 0;
    for (int i = 0; i < steps; ++i)
    {
        std::this_thread::sleep_until(start + std::chrono::microseconds(i * step));
        if (timing.ShouldUpdate())
            ++fires;
    }
    auto end = std::chrono::steady_clock::now();
    int64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    int64_t expected = elapsed_us / freq;

    REQUIRE(fires >= expected - 1);
    REQUIRE(fires <= expected + 1);
}

// T1c: Stall clamp fires once and snaps forward on a large gap.
TEST_CASE("Timing stall clamp prevents burst after long stall", "[timing][T1c]")
{
    constexpr uint32_t freq = 100000; // 10 Hz, 100 ms intervals
    Timing timing(freq);

    // Let it fire once immediately (initialized in the past)
    bool first = timing.ShouldUpdate();
    (void)first;

    // Stall for longer than MAX_CATCHUP * freq (2 * 100ms = 200ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    // Should fire once, then NOT fire again immediately (snapped forward)
    bool after_stall = timing.ShouldUpdate();
    REQUIRE(after_stall == true);

    // Immediately check again — should NOT fire (snapped to now)
    bool immediate = timing.ShouldUpdate();
    REQUIRE(immediate == false);
}

// T1d: Frequency 0 always fires (test fixture path preserved).
TEST_CASE("Timing with frequency 0 always fires", "[timing][T1d]")
{
    Timing timing(0);
    for (int i = 0; i < 100; ++i)
    {
        REQUIRE(timing.ShouldUpdate() == true);
    }
}

// T1b supplement: Verify three-tier collapse produces expected frequencies.
TEST_CASE("Timing three-tier cycle intervals", "[timing][T1b]")
{
    // These are the expected CycleIntervalMicros values from zzt_system.
    // Tier 1 (gameSpeed 1-3): ~54925 µs → ~18.21 Hz
    // Tier 2 (gameSpeed 4-6): ~109849 µs → ~9.10 Hz
    // Tier 3 (gameSpeed 7-9): ~164774 µs → ~6.07 Hz
    // We can't call zzt_system::CycleIntervalMicros here (different package),
    // but we verify the math:
    // PC_TIMER_TICK_SECONDS = 65536.0 / 1193182.0 ≈ 0.0549247
    // Tier 1: ceil(1*2/6) = 1 tick → 1 * 0.0549247 * 1e6 ≈ 54925 µs
    // Tier 2: ceil(4*2/6) = 2 ticks → 2 * 0.0549247 * 1e6 ≈ 109849 µs
    // Tier 3: ceil(7*2/6) = 3 ticks → 3 * 0.0549247 * 1e6 ≈ 164774 µs
    constexpr double pc_tick = 65536.0 / 1193182.0;

    auto tier1_us = static_cast<uint32_t>(std::ceil(1 * pc_tick * 1e6));
    auto tier2_us = static_cast<uint32_t>(std::ceil(2 * pc_tick * 1e6));
    auto tier3_us = static_cast<uint32_t>(std::ceil(3 * pc_tick * 1e6));

    // Tier 1: ~18.21 Hz
    double tier1_hz = 1e6 / tier1_us;
    REQUIRE(tier1_hz > 17.0);
    REQUIRE(tier1_hz < 20.0);

    // Tier 2: ~9.10 Hz
    double tier2_hz = 1e6 / tier2_us;
    REQUIRE(tier2_hz > 8.0);
    REQUIRE(tier2_hz < 10.5);

    // Tier 3: ~6.07 Hz
    double tier3_hz = 1e6 / tier3_us;
    REQUIRE(tier3_hz > 5.0);
    REQUIRE(tier3_hz < 7.5);

    // Verify tier separation
    REQUIRE(tier1_us < tier2_us);
    REQUIRE(tier2_us < tier3_us);
}

namespace
{
    using micros = std::chrono::microseconds;

    constexpr micros us(int64_t count)
    {
        return micros(count);
    }

    // The schedule as it was written when intervals were 32-bit, kept here on 64-bit integers so
    // the new code can be compared with it result for result.
    struct ReferenceSchedule
    {
        uint64_t interval;
        uint64_t last;

        bool step(uint64_t now)
        {
            if (this->interval == 0)
                return true;
            uint64_t elapsed = now - this->last;
            if (elapsed >= this->interval)
            {
                uint64_t intervals = elapsed / this->interval;
                if (intervals > MAX_CATCHUP)
                    this->last = now;
                else
                    this->last += intervals * this->interval;
                return true;
            }
            return false;
        }
    };
}

TEST_CASE("Schedule lengths from one second to thirty days", "[Timing]")
{
    const micros lengths[] = {
        std::chrono::seconds(1),
        std::chrono::minutes(70),
        std::chrono::hours(2),
        std::chrono::hours(24),
        std::chrono::hours(24 * 30),
    };

    for (micros length : lengths)
    {
        INFO("length " << length.count() << " us");

        SECTION("not due one microsecond early, due on time")
        {
            Timing timing(length);
            timing.Restart(us(0));
            REQUIRE_FALSE(timing.ShouldUpdate(length - us(1)));
            REQUIRE(timing.ShouldUpdate(length));
            REQUIRE_FALSE(timing.ShouldUpdate(length));
        }

        SECTION("on a 33333 us pass grid")
        {
            constexpr int64_t grid = 33333;
            Timing timing(length);
            timing.Restart(us(0));
            const int64_t dueStep = (length.count() + grid - 1) / grid;

            // Only the passes around the due time are evaluated. A pass that is not due leaves the
            // schedule alone, so skipping the earlier ones changes nothing.
            for (int64_t step = dueStep - 3; step < dueStep; ++step)
            {
                REQUIRE_FALSE(timing.ShouldUpdate(us(step * grid)));
            }
            REQUIRE(timing.ShouldUpdate(us(dueStep * grid)));
            REQUIRE_FALSE(timing.ShouldUpdate(us((dueStep + 1) * grid)));
        }
    }
}

TEST_CASE("Maximum and invalid intervals", "[Timing]")
{
    SECTION("the maximum is a hundred years")
    {
        REQUIRE(MAX_INTERVAL == std::chrono::hours(24 * 36525));
    }

    SECTION("the maximum is accepted without overflow")
    {
        Timing timing(MAX_INTERVAL);
        REQUIRE(timing.GetInterval() == MAX_INTERVAL);

        timing.Restart(MAX_INTERVAL);
        REQUIRE_FALSE(timing.ShouldUpdate(MAX_INTERVAL + MAX_INTERVAL - us(1)));
        REQUIRE(timing.ShouldUpdate(MAX_INTERVAL + MAX_INTERVAL));

        Timing other(us(1000));
        REQUIRE_NOTHROW(other.SetInterval(MAX_INTERVAL));
        REQUIRE(other.GetInterval() == MAX_INTERVAL);
        other.Restart(us(0));
        REQUIRE_FALSE(other.ShouldUpdate(MAX_INTERVAL - us(1)));
        REQUIRE(other.ShouldUpdate(MAX_INTERVAL));
    }

    SECTION("values outside zero to the maximum are rejected")
    {
        const micros bad[] = {
            MAX_INTERVAL + us(1),
            micros::max(),
            us(-1),
            micros::min(),
        };
        for (micros value : bad)
        {
            INFO("value " << value.count() << " us");
            REQUIRE_THROWS_AS(Timing(value), std::runtime_error);

            Timing timing(us(5000));
            REQUIRE_THROWS_AS(timing.SetInterval(value), std::runtime_error);
            REQUIRE(timing.GetInterval() == us(5000));
        }
    }
}

TEST_CASE("Zero interval is due on every call", "[Timing]")
{
    SECTION("every call is due and the state is left alone")
    {
        Timing timing(us(0));
        timing.Restart(us(1000));
        REQUIRE(timing.ShouldUpdate(us(1000)));
        REQUIRE(timing.ShouldUpdate(us(1000)));
        REQUIRE(timing.ShouldUpdate(us(5000)));
        REQUIRE(timing.ShouldUpdate(us(5000)));
        REQUIRE(timing.ShouldUpdate(us(0)));

        // The schedule still starts at 1000 us because the zero-interval calls did not move it.
        timing.SetInterval(us(10000));
        REQUIRE_FALSE(timing.ShouldUpdate(us(10999)));
        REQUIRE(timing.ShouldUpdate(us(11000)));
    }

    SECTION("an instant earlier than the last one counts as no time passed")
    {
        Timing timing(std::chrono::milliseconds(100));
        timing.Restart(std::chrono::seconds(1));
        REQUIRE_FALSE(timing.ShouldUpdate(std::chrono::milliseconds(500)));
        REQUIRE_FALSE(timing.ShouldUpdate(us(0)));
        REQUIRE_FALSE(timing.ShouldUpdate(std::chrono::milliseconds(1099)));
        REQUIRE(timing.ShouldUpdate(std::chrono::milliseconds(1100)));
    }
}

TEST_CASE("Drift-free on a coarse grid", "[Timing]")
{
    constexpr int64_t grid = 33333;
    constexpr int64_t hour = 3600LL * 1000000LL;
    const int64_t intervals[] = {109850, 54925, 109849, 164774};

    for (int64_t interval : intervals)
    {
        INFO("interval " << interval << " us");
        Timing timing(us(interval));
        timing.Restart(us(0));

        int64_t fires = 0;
        int64_t now = 0;
        for (now = grid; now <= hour; now += grid)
        {
            if (timing.ShouldUpdate(us(now)))
                ++fires;
        }
        const int64_t last = now - grid;
        const int64_t whole = last / interval;
        REQUIRE(fires >= whole - 1);
        REQUIRE(fires <= whole + 1);
    }
}

TEST_CASE("Stall snaps forward", "[Timing]")
{
    constexpr int64_t interval = 100000;

    SECTION("the first call right after creation is not due")
    {
        Timing timing(std::chrono::hours(1));
        REQUIRE_FALSE(timing.ShouldUpdate());
    }

    SECTION("the first fire is one full interval after the start")
    {
        Timing timing(us(interval));
        timing.Restart(us(500));
        REQUIRE_FALSE(timing.ShouldUpdate(us(500)));
        REQUIRE_FALSE(timing.ShouldUpdate(us(500 + interval - 1)));
        REQUIRE(timing.ShouldUpdate(us(500 + interval)));
    }

    SECTION("a gap of two intervals is caught up and stays on the grid")
    {
        Timing timing(us(interval));
        timing.Restart(us(0));
        REQUIRE(timing.ShouldUpdate(us(2 * interval + 50000)));
        REQUIRE_FALSE(timing.ShouldUpdate(us(2 * interval + 50000)));
        REQUIRE_FALSE(timing.ShouldUpdate(us(3 * interval - 1)));
        REQUIRE(timing.ShouldUpdate(us(3 * interval)));
    }

    SECTION("a gap of three or more intervals fires once and snaps forward")
    {
        Timing timing(us(interval));
        timing.Restart(us(0));
        const int64_t stalled = 3 * interval + 50000;
        REQUIRE(timing.ShouldUpdate(us(stalled)));
        REQUIRE_FALSE(timing.ShouldUpdate(us(stalled)));
        // On the old grid the next fire would be at four intervals.
        REQUIRE_FALSE(timing.ShouldUpdate(us(4 * interval)));
        REQUIRE_FALSE(timing.ShouldUpdate(us(stalled + interval - 1)));
        REQUIRE(timing.ShouldUpdate(us(stalled + interval)));
    }
}

TEST_CASE("Sequence matches the old algorithm", "[Timing]")
{
    const int64_t intervals[] = {1, 33333, 54925, 109850, 1000000, 7200LL * 1000000LL};

    for (int64_t interval : intervals)
    {
        INFO("interval " << interval << " us");
        std::mt19937 random(12345);
        Timing timing(us(interval));
        timing.Restart(us(0));
        ReferenceSchedule reference{static_cast<uint64_t>(interval), 0};

        int64_t now = 0;
        for (int i = 0; i < 20000; ++i)
        {
            // Mostly small steps, now and then a stall of many intervals.
            const uint32_t pick = random();
            int64_t step = static_cast<int64_t>(pick % 40000);
            if (pick % 97 == 0)
                step = static_cast<int64_t>(random() % (5 * interval + 1));
            now += step;
            REQUIRE(timing.ShouldUpdate(us(now)) == reference.step(static_cast<uint64_t>(now)));
        }
    }
}

TEST_CASE("GetInterval round-trips", "[Timing]")
{
    REQUIRE(DEFAULT_INTERVAL == us(33333));
    REQUIRE(Timing().GetInterval() == DEFAULT_INTERVAL);

    Timing timing;
    for (int64_t value : {int64_t(0), int64_t(1), int64_t(33333), int64_t(7200) * 1000000, MAX_INTERVAL.count()})
    {
        timing.SetInterval(us(value));
        REQUIRE(timing.GetInterval() == us(value));
    }
}
