#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// This file calls the older names on purpose, to show they still behave as they always did.
// It is the only test file that does so, so the deprecation notices are switched off here.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace
{
    using micros = std::chrono::microseconds;

    // Records what the older delta-time call returns on each update.
    class OldStyleSystem : public ecs::System
    {
      public:
        OldStyleSystem() : System("old-style") {}

        nlohmann::json Export() const override
        {
            return nlohmann::json::object();
        }

        void Update() override
        {
            const uint32_t first = this->DeltaTimeGet();
            for (int i = 0; i < 5; ++i)
            {
                if (this->DeltaTimeGet() != first)
                    this->stable = false;
            }
            this->values.push_back(first);
        }

        size_t TimerTotal() const { return this->timers.size(); }

        std::vector<uint32_t> values;
        bool stable = true;
    };
}

TEST_CASE("Timing built from a number keeps its meaning", "[Compatibility]")
{
    ecs::Timing timing(33333);
    REQUIRE(timing.GetFrequency() == 33333);
    REQUIRE(timing.GetInterval() == micros(33333));

    timing.SetFrequency(250000);
    REQUIRE(timing.GetFrequency() == 250000);
    REQUIRE(timing.GetInterval() == micros(250000));

    timing.Restart(micros(0));
    REQUIRE_FALSE(timing.ShouldUpdate(micros(249999)));
    REQUIRE(timing.ShouldUpdate(micros(250000)));
}

TEST_CASE("A zero frequency is due on every call", "[Compatibility]")
{
    ecs::Timing timing(0);
    timing.Restart(micros(0));
    for (int i = 0; i < 20; ++i)
        REQUIRE(timing.ShouldUpdate(micros(i)));

    ecs::Timing other;
    other.SetFrequency(0);
    REQUIRE(other.GetFrequency() == 0);
    REQUIRE(other.GetInterval() == micros(0));
}

TEST_CASE("GetFrequency saturates for a long interval", "[Compatibility]")
{
    ecs::Timing timing(std::chrono::hours(2));
    REQUIRE(timing.GetFrequency() == std::numeric_limits<uint32_t>::max());
    REQUIRE(timing.GetInterval() == std::chrono::hours(2));
}

TEST_CASE("A timer built from seconds keeps its meaning", "[Compatibility]")
{
    ecs::ManualClock clock(micros(0));
    ecs::Manager manager;
    auto container = manager.Container("old-names");
    auto *system = static_cast<OldStyleSystem *>(container->System(std::make_unique<OldStyleSystem>()));
    container->ClockSet(&clock);
    system->Timing.SetInterval(micros(0));

    int fires = 0;

    SECTION("seven thousand two hundred seconds fires at two hours and not before")
    {
        system->TimerAdd(ecs::Timer("long", [&fires] { fires++; }, 7200, true));
        container->Update();

        clock.Advance(std::chrono::minutes(48));
        container->Update();
        REQUIRE(fires == 0);

        clock.Advance(std::chrono::hours(2) - std::chrono::minutes(48) - micros(1));
        container->Update();
        REQUIRE(fires == 0);

        clock.Advance(micros(1));
        container->Update();
        REQUIRE(fires == 1);
    }

    SECTION("zero fires on every pass")
    {
        system->TimerAdd(ecs::Timer("zero", [&fires] { fires++; }, 0, true));
        for (int i = 0; i < 5; ++i)
        {
            clock.Advance(micros(100));
            container->Update();
        }
        REQUIRE(fires == 5);
    }

    SECTION("a negative number is rejected when the timer is added")
    {
        REQUIRE_THROWS_AS(system->TimerAdd(ecs::Timer("bad", [] {}, -1, true)), std::runtime_error);
        REQUIRE_THROWS_AS(system->TimerAdd(ecs::Timer("big", [] {}, 4294967295u, true)), std::runtime_error);
        REQUIRE(system->TimerTotal() == 0);
    }
}

TEST_CASE("A standalone zero-length timer fires on every call", "[Compatibility]")
{
    int fires = 0;
    ecs::Timer timer("zero", [&fires] { fires++; }, 0, true);
    for (int i = 0; i < 10; ++i)
        timer.CallbackRun();
    REQUIRE(fires == 10);
}

TEST_CASE("DeltaTimeGet is stable within an update and carries its remainder", "[Compatibility]")
{
    ecs::ManualClock clock(micros(1000000));
    ecs::Manager manager;
    auto container = manager.Container("old-delta");
    auto *system = static_cast<OldStyleSystem *>(container->System(std::make_unique<OldStyleSystem>()));
    container->ClockSet(&clock);
    system->Timing.SetInterval(micros(33333));

    constexpr int updates = 301;
    const micros begin = clock.Now();
    for (int i = 0; i < updates; ++i)
    {
        clock.Advance(micros(33333));
        container->Update();
    }

    REQUIRE(system->stable);
    REQUIRE(system->values.size() == static_cast<size_t>(updates));

    // From the second update on the whole milliseconds add up to the clock's total, within one.
    int64_t sum = 0;
    for (size_t i = 1; i < system->values.size(); ++i)
        sum += system->values[i];
    const int64_t clockMs = (33333LL * (updates - 1)) / 1000;
    REQUIRE(sum >= clockMs - 1);
    REQUIRE(sum <= clockMs + 1);
    REQUIRE(clock.Now() - begin == micros(33333LL * updates));

    // A step of 33333 us reads mostly 33 ms, with a 34 about every third update.
    int thirtyFour = 0;
    for (size_t i = 1; i < system->values.size(); ++i)
    {
        REQUIRE((system->values[i] == 33 || system->values[i] == 34));
        if (system->values[i] == 34)
            thirtyFour++;
    }
    REQUIRE(thirtyFour > 0);
    REQUIRE(thirtyFour < updates / 2);
    const std::vector<uint32_t> expected = {33, 33, 33, 34, 33, 33, 34};
    for (size_t i = 0; i < expected.size(); ++i)
        REQUIRE(system->values[i] == expected[i]);
}

namespace
{
    class CountingSystem : public ecs::System
    {
      public:
        explicit CountingSystem(std::atomic<int> *count) : System("counting"), count(count) {}

        nlohmann::json Export() const override
        {
            return nlohmann::json::object();
        }

        void Update() override
        {
            this->count->fetch_add(1);
        }

      private:
        std::atomic<int> *count;
    };
}

TEST_CASE("Start with a number runs the world at that many microseconds", "[Compatibility]")
{
    std::atomic<int> count{0};
    {
        ecs::Manager manager;
        auto container = manager.Container("old-start");
        auto *system = container->System(std::make_unique<CountingSystem>(&count));
        system->Timing.SetInterval(micros(0));
        container->Start(2000);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (count.load() < 3 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
    }
    REQUIRE(count.load() >= 3);
}
