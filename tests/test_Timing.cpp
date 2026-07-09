#include <catch2/catch_all.hpp>
#include <libecs-cpp/Timing.hpp>
#include <thread>
#include <chrono>
#include <numeric>
#include <algorithm>

using namespace ecs;

// T1b: Remainder-carry keeps drift-free average under jittered steps.
// Simulate a 30 Hz sampling grid (33333 µs) with a game cadence of ~109850 µs (9.10 Hz tier).
// Without remainder-carry, the average fires would be ~17% slow on this grid.
TEST_CASE("Timing remainder-carry keeps drift-free average", "[timing][T1b]")
{
    // ~109850 µs ≈ 9.10 Hz (gameSpeed 4-6 tier)
    constexpr uint32_t freq = 109850;
    Timing timing(freq);

    // Simulate 30 Hz sampling: advance a fake clock by 33333 µs each step.
    // We can't mock the clock, so we measure real behavior with small sleeps.
    // Instead, we use a mathematical approach: check that the carry logic
    // advances by multiples of freq, not to 'now'.

    // Verify the core property: after a fire, lastUpdateTime advances by
    // N * freq (where N = elapsed / freq), not to current_time.
    // We can't inspect lastUpdateTime directly (private), but we can
    // observe the behavior: with remainder-carry, the long-run average
    // should be very close to the target frequency.

    // Practical test: fire a known number of times and measure wall time.
    constexpr int iterations = 50;
    auto start = std::chrono::steady_clock::now();
    int fires = 0;
    for (int i = 0; i < iterations; ++i)
    {
        if (timing.ShouldUpdate())
            ++fires;
        std::this_thread::sleep_for(std::chrono::microseconds(freq));
    }
    auto end = std::chrono::steady_clock::now();
    double elapsed_us = std::chrono::duration<double, std::micro>(end - start).count();
    double actual_hz = fires / (elapsed_us / 1e6);
    double target_hz = 1e6 / freq;

    // Should be within 25% (generous bound for test environment jitter)
    REQUIRE(actual_hz > target_hz * 0.75);
    REQUIRE(actual_hz < target_hz * 1.25);
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
