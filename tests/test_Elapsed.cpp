#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Every case here drives time by hand with ecs::ManualClock, so none of them sleeps.

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define ELAPSED_TEST_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define ELAPSED_TEST_SANITIZED 1
#endif
#endif

namespace
{
    using micros = std::chrono::microseconds;

    constexpr micros us(int64_t count)
    {
        return micros(count);
    }

    // The long simulated runs are shortened under the sanitizers, which slow every call down.
#ifdef ELAPSED_TEST_SANITIZED
    constexpr int64_t sanitizerFactor = 20;
#else
    constexpr int64_t sanitizerFactor = 1;
#endif

    // A system that records what it is told about time on each update.
    class Probe : public ecs::System
    {
      public:
        explicit Probe(const std::string &handle = "probe") : System(handle) {}

        nlohmann::json Export() const override
        {
            return nlohmann::json::object();
        }

        void Update() override
        {
            this->updates++;
            const micros first = this->ElapsedGet();
            const double seconds = this->ElapsedSecondsGet();
            for (int i = 1; i < this->reads; ++i)
            {
                if (this->ElapsedGet() != first)
                    this->agree = false;
                if (this->ElapsedSecondsGet() != seconds)
                    this->agree = false;
            }
            if (this->reads > 0)
            {
                this->lastElapsed = first;
                this->lastSeconds = seconds;
                this->total += first;
                if (this->keep)
                    this->values.push_back(first);
            }
            if (this->watch != nullptr)
                this->instants.push_back(this->watch->Now());
            if (this->onUpdate)
                this->onUpdate();
        }

        size_t TimerTotal() const
        {
            return this->timers.size();
        }

        int updates = 0;
        // How many times Update() reads the elapsed time; zero means it never asks.
        int reads = 1;
        bool agree = true;
        bool keep = false;
        micros lastElapsed{-1};
        double lastSeconds = -1;
        micros total{0};
        std::vector<micros> values;
        const ecs::Clock *watch = nullptr;
        std::vector<micros> instants;
        std::function<void()> onUpdate;
    };

    // Moves the clock on and runs one pass of the system.
    void step(Probe &probe, ecs::ManualClock &clock, micros delta)
    {
        clock.Advance(delta);
        probe.UpdateSystem();
    }

    ecs::Timer counter(int &count, micros length, bool repeat = true, const std::string &name = "timer")
    {
        return ecs::Timer(name, [&count] { count++; }, length, repeat);
    }

    Probe *probeAdd(ecs::Container *container, micros interval, const std::string &handle)
    {
        auto *probe = static_cast<Probe *>(container->System(std::make_unique<Probe>(handle)));
        probe->Timing.SetInterval(interval);
        return probe;
    }

    // Updates a lone system with a step per update and checks the elapsed values add up to the
    // time the clock moved between the first and the last update.
    template <typename StepFor>
    void sumCheck(micros start, int64_t updates, StepFor stepFor)
    {
        ecs::ManualClock clock(start);
        Probe probe;
        probe.Timing.SetInterval(us(0));
        probe.ClockSet(&clock);

        probe.UpdateSystem();
        const micros firstInstant = clock.Now();
        micros sum{0};
        for (int64_t i = 0; i < updates; ++i)
        {
            clock.Advance(stepFor(i));
            probe.UpdateSystem();
            sum += probe.lastElapsed;
        }
        REQUIRE(probe.agree);
        const int64_t difference = (sum - (clock.Now() - firstInstant)).count();
        REQUIRE(std::abs(difference) <= 1);
    }
}

TEST_CASE("Timer lengths fire at their length", "[Elapsed]")
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

        SECTION("not one microsecond early, fires on time")
        {
            ecs::ManualClock clock(std::chrono::seconds(5));
            Probe probe;
            probe.ClockSet(&clock);
            int fired = 0;
            probe.TimerAdd(counter(fired, length));

            step(probe, clock, length - us(1));
            REQUIRE(fired == 0);
            step(probe, clock, us(1));
            REQUIRE(fired == 1);
        }

        SECTION("on a 33333 us pass grid it fires within one pass of its length")
        {
            ecs::ManualClock clock(std::chrono::seconds(5));
            Probe probe;
            probe.ClockSet(&clock);
            int fired = 0;
            probe.TimerAdd(counter(fired, length));
            const micros added = clock.Now();

            // Jump to a little before the due time, then run the passes around it.
            step(probe, clock, length - us(100000));
            REQUIRE(fired == 0);
            while (fired == 0)
            {
                step(probe, clock, us(33333));
                REQUIRE(clock.Now() < added + length + us(33333));
            }
            REQUIRE(clock.Now() >= added + length);
            REQUIRE(fired == 1);
        }
    }

    SECTION("a two hour timer is not due at forty-eight minutes")
    {
        ecs::ManualClock clock;
        Probe probe;
        probe.ClockSet(&clock);
        int fired = 0;
        probe.TimerAdd(counter(fired, std::chrono::hours(2)));
        step(probe, clock, std::chrono::minutes(48));
        REQUIRE(fired == 0);
        step(probe, clock, std::chrono::minutes(48));
        REQUIRE(fired == 0);
        step(probe, clock, std::chrono::minutes(24));
        REQUIRE(fired == 1);
    }

    SECTION("the first fire is one length after the timer is added")
    {
        ecs::ManualClock clock;
        Probe probe;
        probe.ClockSet(&clock);
        step(probe, clock, std::chrono::hours(10));

        int fired = 0;
        probe.TimerAdd(counter(fired, std::chrono::hours(1)));
        step(probe, clock, std::chrono::hours(1) - us(1));
        REQUIRE(fired == 0);
        step(probe, clock, us(1));
        REQUIRE(fired == 1);
    }

    SECTION("a repeating timer keeps its period")
    {
        ecs::ManualClock clock;
        Probe probe;
        probe.ClockSet(&clock);
        int fired = 0;
        probe.TimerAdd(counter(fired, std::chrono::hours(2)));
        for (int i = 1; i <= 3; ++i)
        {
            step(probe, clock, std::chrono::hours(2) - us(1));
            REQUIRE(fired == i - 1);
            step(probe, clock, us(1));
            REQUIRE(fired == i);
        }
    }
}

TEST_CASE("Invalid timer lengths are rejected when added", "[Elapsed]")
{
    ecs::ManualClock clock;
    Probe probe;
    probe.ClockSet(&clock);
    int fired = 0;

    SECTION("durations outside zero to the maximum")
    {
        const micros bad[] = {us(-1), std::chrono::seconds(-30), ecs::MAX_INTERVAL + us(1), micros::max()};
        for (micros length : bad)
        {
            INFO("length " << length.count() << " us");
            ecs::Timer timer("bad", [&fired] { fired++; }, length);
            REQUIRE_THROWS_AS(probe.TimerAdd(timer), std::runtime_error);
            REQUIRE_THROWS_WITH(probe.TimerAdd(timer), Catch::Matchers::ContainsSubstring("outside 0 to"));
            REQUIRE(probe.TimerTotal() == 0);
        }
        step(probe, clock, std::chrono::hours(1));
        REQUIRE(fired == 0);
    }

    SECTION("the maximum is accepted")
    {
        REQUIRE_NOTHROW(probe.TimerAdd(counter(fired, ecs::MAX_INTERVAL)));
        REQUIRE(probe.TimerTotal() == 1);
        step(probe, clock, ecs::MAX_INTERVAL - us(1));
        REQUIRE(fired == 0);
        step(probe, clock, us(1));
        REQUIRE(fired == 1);
    }

    SECTION("a bare number is still seconds, and bad ones are rejected rather than wrapped")
    {
        // These calls use the older constructor that takes a count of seconds on purpose.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        ecs::Timer negative("negative", [&fired] { fired++; }, -1);
        ecs::Timer huge("huge", [&fired] { fired++; }, 4294967295u);
        ecs::Timer hours("hours", [&fired] { fired++; }, 7200);
#pragma GCC diagnostic pop
        REQUIRE_THROWS_AS(probe.TimerAdd(negative), std::runtime_error);
        REQUIRE_THROWS_AS(probe.TimerAdd(huge), std::runtime_error);
        REQUIRE(probe.TimerTotal() == 0);

        REQUIRE_NOTHROW(probe.TimerAdd(hours));
        step(probe, clock, std::chrono::minutes(48));
        REQUIRE(fired == 0);
        step(probe, clock, std::chrono::minutes(72));
        REQUIRE(fired == 1);
    }
}

TEST_CASE("Zero-length timers fire once per pass", "[Elapsed]")
{
    ecs::ManualClock clock(std::chrono::seconds(3));
    Probe probe;
    probe.ClockSet(&clock);

    SECTION("a repeating zero timer fires once in every pass, even when the clock does not move")
    {
        int fired = 0;
        probe.TimerAdd(counter(fired, us(0)));
        for (int pass = 1; pass <= 5; ++pass)
        {
            probe.UpdateSystem();
            REQUIRE(fired == pass);
        }
        step(probe, clock, std::chrono::hours(1));
        REQUIRE(fired == 6);
    }

    SECTION("a one-shot zero timer fires once and is discarded")
    {
        int fired = 0;
        probe.TimerAdd(counter(fired, us(0), false));
        probe.UpdateSystem();
        REQUIRE(fired == 1);
        REQUIRE(probe.TimerTotal() == 0);
        probe.UpdateSystem();
        step(probe, clock, std::chrono::seconds(10));
        REQUIRE(fired == 1);
    }

    SECTION("a callback that adds a timer does not make its own timer loop")
    {
        int fired = 0;
        int added = 0;
        probe.TimerAdd(ecs::Timer("A", [&] {
            fired++;
            if (added < 3)
            {
                added++;
                probe.TimerAdd(ecs::Timer("extra" + std::to_string(added), [] {}, us(0)));
            }
        }, us(0)));
        probe.UpdateSystem();
        REQUIRE(fired == 1);
        probe.UpdateSystem();
        REQUIRE(fired == 2);
        probe.UpdateSystem();
        REQUIRE(fired == 3);
        REQUIRE(probe.TimerTotal() == 4);
    }

    SECTION("a callback that clears and re-adds itself fires once per pass")
    {
        int fired = 0;
        std::function<void()> callback;
        callback = [&] {
            if (fired >= 100)
                return;
            fired++;
            probe.TimerClear("A");
            probe.TimerAdd(ecs::Timer("A", callback, us(0)));
        };
        probe.TimerAdd(ecs::Timer("A", callback, us(0)));
        for (int pass = 1; pass <= 4; ++pass)
        {
            probe.UpdateSystem();
            REQUIRE(fired == pass);
            REQUIRE(probe.TimerTotal() == 1);
        }
    }

    SECTION("a callback that clears another timer stops it from firing")
    {
        int first = 0;
        int second = 0;
        probe.TimerAdd(ecs::Timer("first", [&] {
            first++;
            probe.TimerClear("second");
        }, us(0)));
        probe.TimerAdd(counter(second, us(0), true, "second"));
        probe.UpdateSystem();
        probe.UpdateSystem();
        REQUIRE(first == 2);
        REQUIRE(second == 0);
    }

    SECTION("a system with interval zero updates on every pass of the world")
    {
        ecs::Manager manager;
        auto container = manager.Container("zero-world");
        container->ClockSet(&clock);
        auto *zero = probeAdd(container, us(0), "zero");
        for (int pass = 1; pass <= 10; ++pass)
        {
            container->Update();
            REQUIRE(zero->updates == pass);
        }
        clock.Advance(std::chrono::seconds(1));
        container->Update();
        REQUIRE(zero->updates == 11);
        REQUIRE(zero->lastElapsed == std::chrono::seconds(1));
    }
}

TEST_CASE("Elapsed sums match the clock", "[Elapsed]")
{
    const int64_t hour = 3600LL * 1000000LL / sanitizerFactor;

    SECTION("30 Hz")
    {
        sumCheck(us(777), hour / 33333, [](int64_t) { return us(33333); });
    }

    SECTION("120 Hz")
    {
        sumCheck(us(777), hour / 8333, [](int64_t) { return us(8333); });
    }

    SECTION("1000 Hz")
    {
        sumCheck(us(777), hour / 1000, [](int64_t) { return us(1000); });
    }

    SECTION("400 us steps over a million updates")
    {
        sumCheck(us(0), 1000000 / sanitizerFactor, [](int64_t) { return us(400); });
    }

    SECTION("alternating 33333 and 33334 us steps")
    {
        sumCheck(us(12345), hour / 33333, [](int64_t i) { return us(i % 2 == 0 ? 33333 : 33334); });
    }

    SECTION("uneven steps")
    {
        sumCheck(us(1), 50000, [](int64_t i) { return us(1 + (i * 7919) % 100003); });
    }
}

TEST_CASE("Sub-millisecond updates report the real gap", "[Elapsed]")
{
    ecs::ManualClock clock(std::chrono::seconds(1));
    Probe probe;
    probe.Timing.SetInterval(us(0));
    probe.ClockSet(&clock);
    probe.UpdateSystem();

    step(probe, clock, us(400));
    REQUIRE(probe.lastElapsed == us(400));
    REQUIRE(probe.lastSeconds == Catch::Approx(0.0004).epsilon(1e-12));

    step(probe, clock, us(1));
    REQUIRE(probe.lastElapsed == us(1));

    probe.UpdateSystem();
    REQUIRE(probe.lastElapsed == us(0));
    REQUIRE(probe.lastSeconds == 0.0);
}

TEST_CASE("Reads within an update agree", "[Elapsed]")
{
    ecs::ManualClock clock;
    Probe probe;
    probe.Timing.SetInterval(us(0));
    probe.ClockSet(&clock);
    probe.UpdateSystem();

    SECTION("one, two and a hundred reads give one value")
    {
        for (int reads : {1, 2, 100})
        {
            probe.reads = reads;
            const micros gap = us(1000 + reads);
            step(probe, clock, gap);
            REQUIRE(probe.agree);
            REQUIRE(probe.lastElapsed == gap);
            REQUIRE(probe.lastSeconds == Catch::Approx(static_cast<double>(gap.count()) / 1e6).epsilon(1e-12));
        }
    }

    SECTION("reads in a timer callback equal reads in Update")
    {
        std::vector<micros> inCallback;
        std::vector<micros> inUpdate;
        probe.TimerAdd(ecs::Timer("reader", [&] {
            inCallback.push_back(probe.ElapsedGet());
            inCallback.push_back(probe.ElapsedGet());
        }, us(0)));
        probe.onUpdate = [&] { inUpdate.push_back(probe.ElapsedGet()); };

        for (int i = 1; i <= 5; ++i)
        {
            step(probe, clock, us(250 * i));
            REQUIRE(inCallback[2 * (i - 1)] == us(250 * i));
            REQUIRE(inCallback[2 * (i - 1) + 1] == us(250 * i));
            REQUIRE(inUpdate.back() == us(250 * i));
        }
    }

    SECTION("a system that never reads loses nothing")
    {
        probe.reads = 0;
        step(probe, clock, us(5000));
        step(probe, clock, us(6000));
        step(probe, clock, us(7000));
        probe.reads = 1;
        step(probe, clock, us(8000));
        REQUIRE(probe.lastElapsed == us(8000));
    }

    SECTION("seconds are the microseconds divided by a million")
    {
        step(probe, clock, us(1234567));
        REQUIRE(probe.ElapsedGet() == us(1234567));
        REQUIRE(probe.ElapsedSecondsGet() == Catch::Approx(1.234567).epsilon(1e-12));
    }
}

TEST_CASE("First update reports the interval", "[Elapsed]")
{
    ecs::ManualClock clock;

    SECTION("a system built an hour before its first update")
    {
        Probe probe;
        probe.Timing.SetInterval(std::chrono::milliseconds(20));
        probe.ClockSet(&clock);
        clock.Advance(std::chrono::hours(1));
        probe.UpdateSystem();
        REQUIRE(probe.lastElapsed == std::chrono::milliseconds(20));
        REQUIRE(probe.lastSeconds == Catch::Approx(0.02).epsilon(1e-12));

        step(probe, clock, std::chrono::milliseconds(35));
        REQUIRE(probe.lastElapsed == std::chrono::milliseconds(35));
    }

    SECTION("the default interval is reported when none was set")
    {
        Probe probe;
        probe.ClockSet(&clock);
        clock.Advance(std::chrono::hours(1));
        probe.UpdateSystem();
        REQUIRE(probe.lastElapsed == ecs::DEFAULT_INTERVAL);
    }

    SECTION("a read before any update gives the interval")
    {
        Probe probe;
        probe.Timing.SetInterval(std::chrono::milliseconds(50));
        probe.ClockSet(&clock);
        REQUIRE(probe.ElapsedGet() == std::chrono::milliseconds(50));
        clock.Advance(std::chrono::minutes(5));
        REQUIRE(probe.ElapsedGet() == std::chrono::milliseconds(50));
        REQUIRE(probe.ElapsedSecondsGet() == Catch::Approx(0.05).epsilon(1e-12));
    }

    SECTION("an interval changed before the first update is the one reported")
    {
        Probe probe;
        probe.Timing.SetInterval(std::chrono::milliseconds(50));
        probe.ClockSet(&clock);
        probe.Timing.SetInterval(std::chrono::milliseconds(80));
        REQUIRE(probe.ElapsedGet() == std::chrono::milliseconds(80));
        clock.Advance(std::chrono::hours(1));
        probe.UpdateSystem();
        REQUIRE(probe.lastElapsed == std::chrono::milliseconds(80));
    }

    SECTION("a zero interval reports zero")
    {
        Probe probe;
        probe.Timing.SetInterval(us(0));
        probe.ClockSet(&clock);
        REQUIRE(probe.ElapsedGet() == us(0));
        clock.Advance(std::chrono::hours(1));
        probe.UpdateSystem();
        REQUIRE(probe.lastElapsed == us(0));
        step(probe, clock, us(900));
        REQUIRE(probe.lastElapsed == us(900));
    }

    SECTION("systems in a world first updated an hour later each report their own interval")
    {
        ecs::Manager manager;
        auto container = manager.Container("late-world");
        container->ClockSet(&clock);
        auto *fast = probeAdd(container, std::chrono::milliseconds(10), "fast");
        auto *slow = probeAdd(container, std::chrono::milliseconds(250), "slow");
        clock.Advance(std::chrono::hours(1));
        container->Update();
        REQUIRE(fast->updates == 1);
        REQUIRE(slow->updates == 1);
        REQUIRE(fast->lastElapsed == std::chrono::milliseconds(10));
        REQUIRE(slow->lastElapsed == std::chrono::milliseconds(250));
    }

    SECTION("the order systems are built in makes no difference")
    {
        Probe a("a");
        a.Timing.SetInterval(std::chrono::milliseconds(40));
        a.ClockSet(&clock);
        clock.Advance(std::chrono::seconds(5));
        Probe b("b");
        b.Timing.SetInterval(std::chrono::milliseconds(70));
        b.ClockSet(&clock);
        clock.Advance(std::chrono::seconds(5));

        b.UpdateSystem();
        a.UpdateSystem();
        REQUIRE(a.lastElapsed == std::chrono::milliseconds(40));
        REQUIRE(b.lastElapsed == std::chrono::milliseconds(70));
    }
}

TEST_CASE("A stall is reported in full", "[Elapsed]")
{
    SECTION("a system's elapsed time is not clamped and its schedule snaps forward")
    {
        ecs::ManualClock clock(std::chrono::hours(1));
        ecs::Manager manager;
        auto container = manager.Container("stall-world");
        container->ClockSet(&clock);
        auto *probe = probeAdd(container, std::chrono::milliseconds(100), "stalled");

        clock.Advance(std::chrono::milliseconds(100));
        container->Update();
        REQUIRE(probe->updates == 1);

        clock.Advance(std::chrono::hours(5));
        container->Update();
        REQUIRE(probe->updates == 2);
        REQUIRE(probe->lastElapsed == std::chrono::hours(5));
        REQUIRE(probe->lastSeconds == Catch::Approx(5.0 * 3600.0).epsilon(1e-12));

        // Fired once for the stall; the next pass is a full interval later, not a burst.
        container->Update();
        REQUIRE(probe->updates == 2);
        clock.Advance(std::chrono::milliseconds(100) - us(1));
        container->Update();
        REQUIRE(probe->updates == 2);
        clock.Advance(us(1));
        container->Update();
        REQUIRE(probe->updates == 3);
        REQUIRE(probe->lastElapsed == std::chrono::milliseconds(100));
    }

    SECTION("a timer fires once for a stall and then waits a full length")
    {
        ecs::ManualClock clock;
        Probe probe;
        probe.ClockSet(&clock);
        int fired = 0;
        probe.TimerAdd(counter(fired, std::chrono::seconds(1)));

        // The first update only reports the interval, so the stall comes after it.
        probe.UpdateSystem();
        REQUIRE(fired == 0);

        step(probe, clock, std::chrono::hours(5));
        REQUIRE(probe.lastElapsed == std::chrono::hours(5));
        REQUIRE(fired == 1);

        step(probe, clock, std::chrono::seconds(1) - us(1));
        REQUIRE(fired == 1);
        step(probe, clock, us(1));
        REQUIRE(fired == 2);
    }
}

TEST_CASE("Changing the interval while running", "[Elapsed]")
{
    ecs::ManualClock clock(std::chrono::seconds(10));
    ecs::Manager manager;
    auto container = manager.Container("interval-world");
    container->ClockSet(&clock);
    auto *probe = probeAdd(container, std::chrono::milliseconds(100), "changing");
    probe->watch = &clock;
    probe->keep = true;

    auto run = [&](int passes) {
        for (int i = 0; i < passes; ++i)
        {
            clock.Advance(std::chrono::milliseconds(10));
            container->Update();
        }
    };

    run(33);
    REQUIRE(probe->updates == 3);

    SECTION("a shorter interval causes no burst and no skipped update")
    {
        probe->Timing.SetInterval(std::chrono::milliseconds(50));
        const size_t before = probe->instants.size();
        run(100);
        REQUIRE(probe->instants.size() - before == 20);
        for (size_t i = before; i < probe->instants.size(); ++i)
        {
            REQUIRE(probe->values[i] == probe->instants[i] - probe->instants[i - 1]);
            REQUIRE(probe->values[i] == std::chrono::milliseconds(50));
        }
    }

    SECTION("a longer interval causes no early update and the gap is real")
    {
        probe->Timing.SetInterval(std::chrono::milliseconds(200));
        const size_t before = probe->instants.size();
        run(100);
        REQUIRE(probe->instants.size() - before == 5);
        for (size_t i = before; i < probe->instants.size(); ++i)
        {
            REQUIRE(probe->values[i] == probe->instants[i] - probe->instants[i - 1]);
            REQUIRE(probe->values[i] >= std::chrono::milliseconds(200));
        }
    }
}

TEST_CASE("A throwing timer callback", "[Elapsed]")
{
    ecs::ManualClock clock(std::chrono::seconds(1));
    ecs::Manager manager;
    auto container = manager.Container("throwing-world");
    std::vector<std::string> logged;
    container->LoggerSet([&](const std::string &message, const std::string &) { logged.push_back(message); });
    container->ClockSet(&clock);
    auto *probe = probeAdd(container, us(0), "throwing-system");

    SECTION("a repeating timer is scheduled again before the callback runs")
    {
        int fired = 0;
        probe->TimerAdd(ecs::Timer("boom", [&] {
            fired++;
            throw std::runtime_error("deliberate failure");
        }, std::chrono::seconds(1)));

        clock.Advance(std::chrono::seconds(1) - us(1));
        REQUIRE_NOTHROW(container->Update());
        REQUIRE(fired == 0);

        clock.Advance(us(1));
        REQUIRE_THROWS_AS(container->Update(), std::runtime_error);
        REQUIRE(fired == 1);
        REQUIRE(probe->TimerTotal() == 1);

        bool mentionsSystem = false;
        for (const auto &message : logged)
        {
            if (message.find("throwing-system") != std::string::npos)
                mentionsSystem = true;
        }
        REQUIRE(mentionsSystem);

        clock.Advance(std::chrono::seconds(1) - us(1));
        REQUIRE_NOTHROW(container->Update());
        REQUIRE(fired == 1);
        clock.Advance(us(1));
        REQUIRE_THROWS_AS(container->Update(), std::runtime_error);
        REQUIRE(fired == 2);
    }

    SECTION("a one-shot timer is discarded")
    {
        int fired = 0;
        probe->TimerAdd(ecs::Timer("boom", [&] {
            fired++;
            throw std::runtime_error("deliberate failure");
        }, std::chrono::seconds(1), false));

        clock.Advance(std::chrono::seconds(1));
        REQUIRE_THROWS_AS(container->Update(), std::runtime_error);
        REQUIRE(fired == 1);
        REQUIRE(probe->TimerTotal() == 0);

        clock.Advance(std::chrono::seconds(10));
        REQUIRE_NOTHROW(container->Update());
        REQUIRE(fired == 1);
    }
}

TEST_CASE("Systems with different intervals", "[Elapsed]")
{
    ecs::ManualClock clock(std::chrono::seconds(1));
    ecs::Manager manager;
    auto container = manager.Container("mixed-world");

    SECTION("each system gets its own elapsed value")
    {
        container->ClockSet(&clock);
        auto *tenth = probeAdd(container, std::chrono::milliseconds(100), "tenth");
        auto *quarter = probeAdd(container, std::chrono::milliseconds(250), "quarter");
        auto *always = probeAdd(container, us(0), "always");
        tenth->keep = quarter->keep = always->keep = true;

        for (int pass = 0; pass < 20; ++pass)
        {
            clock.Advance(std::chrono::milliseconds(50));
            container->Update();
        }

        REQUIRE(tenth->updates == 10);
        REQUIRE(quarter->updates == 4);
        REQUIRE(always->updates == 20);
        for (micros value : tenth->values)
            REQUIRE(value == std::chrono::milliseconds(100));
        for (micros value : quarter->values)
            REQUIRE(value == std::chrono::milliseconds(250));
        REQUIRE(always->values.front() == us(0));
        for (size_t i = 1; i < always->values.size(); ++i)
            REQUIRE(always->values[i] == std::chrono::milliseconds(50));
    }

    SECTION("a system registered after the clock was set uses it")
    {
        container->ClockSet(&clock);
        clock.Advance(std::chrono::seconds(30));
        auto *late = probeAdd(container, std::chrono::milliseconds(100), "late");

        clock.Advance(std::chrono::milliseconds(50));
        container->Update();
        REQUIRE(late->updates == 0);
        clock.Advance(std::chrono::milliseconds(50));
        container->Update();
        REQUIRE(late->updates == 1);
        REQUIRE(late->lastElapsed == std::chrono::milliseconds(100));
        clock.Advance(std::chrono::milliseconds(100));
        container->Update();
        REQUIRE(late->updates == 2);
    }

    SECTION("a system registered before the clock was set moves to it")
    {
        auto *early = probeAdd(container, std::chrono::milliseconds(100), "early");
        container->ClockSet(&clock);

        clock.Advance(std::chrono::milliseconds(99));
        container->Update();
        REQUIRE(early->updates == 0);
        clock.Advance(std::chrono::milliseconds(1));
        container->Update();
        REQUIRE(early->updates == 1);
        REQUIRE(early->lastElapsed == std::chrono::milliseconds(100));
    }

    SECTION("timers in a world on a manual clock fire without sleeping")
    {
        container->ClockSet(&clock);
        auto *probe = probeAdd(container, us(0), "timed");
        int fired = 0;
        probe->TimerAdd(counter(fired, std::chrono::hours(2)));
        clock.Advance(std::chrono::minutes(119));
        container->Update();
        REQUIRE(fired == 0);
        clock.Advance(std::chrono::minutes(1));
        container->Update();
        REQUIRE(fired == 1);
    }
}

TEST_CASE("Start with a duration rejects bad values", "[Elapsed]")
{
    ecs::Manager manager;
    auto container = manager.Container("start-world");
    std::atomic<int> updates{0};
    auto *probe = probeAdd(container, us(0), "start-probe");
    probe->onUpdate = [&updates] { updates++; };

    REQUIRE_THROWS_AS(container->Start(us(-1)), std::runtime_error);
    REQUIRE_THROWS_AS(container->Start(ecs::MAX_INTERVAL + us(1)), std::runtime_error);
    REQUIRE_THROWS_AS(container->Start(micros::max()), std::runtime_error);
    REQUIRE_THROWS_AS(container->Start(micros::min()), std::runtime_error);

    // No thread was started by any of those calls.
    REQUIRE(updates.load() == 0);

    // The world can still be started: the rejected calls did not use up the start.
    REQUIRE_NOTHROW(container->Start(std::chrono::milliseconds(1)));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (updates.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    container->Stop();
    REQUIRE(updates.load() > 0);
}
