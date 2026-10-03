#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class TestSystem : public ecs::System
{
  public:
    TestSystem()
    {
        this->Handle = "TestSystem";
    }

    TestSystem(std::string handle)
    {
        this->Handle = handle;
    }

    nlohmann::json Export() const
    {
        nlohmann::json config;
        return config;
    }
    
};

TEST_CASE("System is initialized with a valid handle", "[System]") {
    TestSystem system;
    REQUIRE_FALSE(system.Handle.empty());
}

TEST_CASE("System can be initialized with a custom handle", "[System]") {
    std::string customHandle = "custom_handle";
    TestSystem system(customHandle);
    REQUIRE(system.Handle == customHandle);
}

TEST_CASE("System starts with no messages", "[System]") {
    TestSystem system;
    REQUIRE(system.MessagesWaiting() == 0);
}

TEST_CASE("System can submit messages", "[System]") {
    TestSystem system;
    nlohmann::json message = {{"key", "value"}};
    system.MessageSubmit(message);
    REQUIRE(system.MessagesWaiting() == 1);
}

TEST_CASE("System reports the time between updates", "[System]") {
    ecs::ManualClock clock(std::chrono::microseconds(5000000));
    TestSystem system;
    system.ClockSet(&clock);
    system.Timing.SetInterval(std::chrono::milliseconds(10));

    // The first update reports the configured interval.
    system.UpdateSystem();
    REQUIRE(system.ElapsedGet() == std::chrono::milliseconds(10));

    // Later updates report the gap on the clock, however often it is read.
    clock.Advance(std::chrono::microseconds(12500));
    system.UpdateSystem();
    REQUIRE(system.ElapsedGet() == std::chrono::microseconds(12500));
    REQUIRE(system.ElapsedGet() == std::chrono::microseconds(12500));
    REQUIRE(system.ElapsedSecondsGet() == Catch::Approx(0.0125));
    REQUIRE(system.ElapsedSecondsGet() == Catch::Approx(0.0125));
}

namespace
{
    struct TimerLog
    {
        std::map<std::string, int> counts;
        std::vector<std::string> messages;

        int count(const std::string &key) const
        {
            auto found = this->counts.find(key);
            return found == this->counts.end() ? 0 : found->second;
        }
    };

    std::string timerHandle(const std::string &name)
    {
        return "timer-system-handle-longer-than-32-chars-" + name;
    }

    class TimerSystem : public ecs::System
    {
      public:
        TimerSystem(TimerLog *log, const std::string &name):
            name(name), log(log)
        {
            this->Handle = timerHandle(name);
            this->Timing.SetInterval(std::chrono::microseconds(0));
        }
        ~TimerSystem() override { this->log->counts["destroyed:" + this->name]++; }

        nlohmann::json Export() const override { return {{"Handle", this->Handle}}; }
        void Update() override
        {
            this->log->counts["update:" + this->name]++;
            this->log->counts["timers-at-update:" + this->name] = (int)this->timers.size();
            if(this->onUpdate) this->onUpdate();
        }

        size_t TimerCount(const std::string &timerName) const
        {
            size_t total = 0;
            for(const auto &timer : this->timers)
            {
                if(timer.Name == timerName) total++;
            }
            return total;
        }
        size_t TimerTotal() const { return this->timers.size(); }

        std::string name;
        std::function<void()> onUpdate;

      private:
        TimerLog *log;
    };

    TimerSystem *timerSystemAdd(ecs::Container *container, TimerLog &log, const std::string &name)
    {
        return static_cast<TimerSystem *>(container->System(std::make_unique<TimerSystem>(&log, name)));
    }

    std::function<void()> fireCounter(TimerLog &log, const std::string &key)
    {
        return [&log, key] { log.counts["fire:" + key]++; };
    }

    void timerAdd(TimerSystem *system, const std::string &name, std::function<void()> callback, bool repeat = true)
    {
        system->TimerAdd(ecs::Timer(name, callback, std::chrono::microseconds(0), repeat));
    }
}

TEST_CASE("A timer callback can add timers", "[System]") {
    // Declared first so it outlives the manager and the systems it releases.
    TimerLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    auto s = timerSystemAdd(container, log, "S");

    SECTION("a callback adds a timer on the first pass only") {
        bool added = false;
        timerAdd(s, "A", [&] {
            log.counts["fire:A"]++;
            if(!added)
            {
                added = true;
                s->TimerAdd(ecs::Timer("C", fireCounter(log, "C"), std::chrono::microseconds(0)));
            }
        });
        timerAdd(s, "B", fireCounter(log, "B"));

        container->Update();
        REQUIRE(log.count("fire:A") == 1);
        REQUIRE(log.count("fire:B") == 1);
        REQUIRE(log.count("fire:C") == 0);
        REQUIRE(log.count("update:S") == 1);
        REQUIRE(log.count("timers-at-update:S") == 3);

        container->Update();
        REQUIRE(log.count("fire:A") == 2);
        REQUIRE(log.count("fire:B") == 2);
        REQUIRE(log.count("fire:C") == 1);
        REQUIRE(s->TimerTotal() == 3);
    }

    SECTION("a callback adds a timer while a sibling is still pending") {
        s->TimerAdd(ecs::Timer("a", [&] {
            log.counts["fire:a"]++;
            s->TimerAdd(ecs::Timer("next", [] {}, std::chrono::microseconds(0)));
        }, std::chrono::microseconds(0)));
        s->TimerAdd(ecs::Timer("b", fireCounter(log, "b"), std::chrono::microseconds(0)));

        container->Update();
        REQUIRE(log.count("fire:a") == 1);
        REQUIRE(log.count("fire:b") == 1);
        REQUIRE(s->TimerCount("next") == 1);
        REQUIRE(s->TimerTotal() == 3);
        REQUIRE(log.count("update:S") == 1);
    }
}

TEST_CASE("A timer callback can add a timer to another system", "[System]") {
    // Declared first so it outlives the manager and the systems it releases.
    TimerLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    auto s0 = timerSystemAdd(container, log, "S0");
    auto s1 = timerSystemAdd(container, log, "S1");
    auto s2 = timerSystemAdd(container, log, "S2");
    timerAdd(s2, "X", fireCounter(log, "X"));

    SECTION("a system registered later fires the new timer in the same pass") {
        bool added = false;
        timerAdd(s1, "A", [&] {
            if(added) return;
            added = true;
            s2->TimerAdd(ecs::Timer("N", fireCounter(log, "N"), std::chrono::microseconds(0)));
        });

        container->Update();
        REQUIRE(log.count("fire:X") == 1);
        REQUIRE(log.count("fire:N") == 1);

        container->Update();
        REQUIRE(log.count("fire:X") == 2);
        REQUIRE(log.count("fire:N") == 2);
    }

    SECTION("a system registered earlier fires the new timer from the next pass") {
        bool added = false;
        timerAdd(s1, "A", [&] {
            if(added) return;
            added = true;
            s0->TimerAdd(ecs::Timer("N", fireCounter(log, "N"), std::chrono::microseconds(0)));
        });

        container->Update();
        REQUIRE(log.count("fire:N") == 0);
        REQUIRE(s0->TimerCount("N") == 1);

        container->Update();
        REQUIRE(log.count("fire:N") == 1);
    }
}

TEST_CASE("A timer callback can cancel itself and a later sibling", "[System]") {
    // Declared first so it outlives the manager and the systems it releases.
    TimerLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    auto s = timerSystemAdd(container, log, "S");

    SECTION("a callback cancels itself and a later sibling") {
        timerAdd(s, "A", [&] {
            log.counts["fire:A"]++;
            s->TimerClear("A");
            s->TimerClear("B");
        });
        timerAdd(s, "B", fireCounter(log, "B"));

        for(int i = 0; i < 3; i++) container->Update();
        REQUIRE(log.count("fire:A") == 1);
        REQUIRE(log.count("fire:B") == 0);
        REQUIRE(s->TimerCount("A") == 0);
        REQUIRE(s->TimerCount("B") == 0);
        REQUIRE(s->TimerTotal() == 0);
        REQUIRE(log.count("update:S") == 3);
    }

    SECTION("a callback cancels only itself") {
        timerAdd(s, "A", [&] {
            log.counts["fire:A"]++;
            s->TimerClear("A");
        });
        timerAdd(s, "B", fireCounter(log, "B"));

        for(int i = 0; i < 3; i++) container->Update();
        REQUIRE(log.count("fire:A") == 1);
        REQUIRE(log.count("fire:B") == 3);
        REQUIRE(s->TimerCount("A") == 0);
        REQUIRE(s->TimerCount("B") == 1);
    }

    SECTION("cancelling a name cancels every timer with that name") {
        timerAdd(s, "A", [&] {
            log.counts["fire:A"]++;
            s->TimerClear("B");
        });
        timerAdd(s, "B", fireCounter(log, "B"));
        timerAdd(s, "B", fireCounter(log, "B"));

        for(int i = 0; i < 3; i++) container->Update();
        REQUIRE(log.count("fire:A") == 3);
        REQUIRE(log.count("fire:B") == 0);
        REQUIRE(s->TimerCount("B") == 0);
        REQUIRE(s->TimerCount("A") == 1);
    }
}

TEST_CASE("A one-shot timer can re-arm itself under the same name", "[System]") {
    // Declared first so it outlives the manager and the systems it releases.
    TimerLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    auto s = timerSystemAdd(container, log, "S");

    std::function<void()> callback;
    callback = [&] {
        log.counts["fire:A"]++;
        s->TimerAdd(ecs::Timer("A", callback, std::chrono::microseconds(0), false));
    };
    s->TimerAdd(ecs::Timer("A", callback, std::chrono::microseconds(0), false));

    for(int pass = 1; pass <= 3; pass++)
    {
        container->Update();
        REQUIRE(log.count("fire:A") == pass);
        REQUIRE(s->TimerCount("A") == 1);
        REQUIRE(s->TimerTotal() == 1);
    }
    REQUIRE(log.count("update:S") == 3);
}

TEST_CASE("Cancel then add under the same name leaves only the new timer", "[System]") {
    // Declared first so it outlives the manager and the systems it releases.
    TimerLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    auto s = timerSystemAdd(container, log, "S");

    SECTION("a callback replaces itself") {
        bool replaced = false;
        timerAdd(s, "A", [&] {
            log.counts["fire:old"]++;
            if(replaced) return;
            replaced = true;
            s->TimerClear("A");
            s->TimerAdd(ecs::Timer("A", fireCounter(log, "new"), std::chrono::microseconds(0)));
        });

        container->Update();
        REQUIRE(s->TimerCount("A") == 1);
        REQUIRE(s->TimerTotal() == 1);
        REQUIRE(log.count("fire:old") == 1);
        REQUIRE(log.count("fire:new") == 0);

        container->Update();
        REQUIRE(log.count("fire:old") == 1);
        REQUIRE(log.count("fire:new") == 1);
        REQUIRE(s->TimerCount("A") == 1);
    }

    SECTION("add then cancel removes both") {
        timerAdd(s, "A", [&] {
            log.counts["fire:A"]++;
            s->TimerAdd(ecs::Timer("X", fireCounter(log, "X"), std::chrono::microseconds(0)));
            s->TimerClear("X");
        }, false);

        container->Update();
        container->Update();
        REQUIRE(log.count("fire:A") == 1);
        REQUIRE(log.count("fire:X") == 0);
        REQUIRE(s->TimerCount("X") == 0);
        REQUIRE(s->TimerTotal() == 0);
    }
}

TEST_CASE("A timer callback can remove its own system", "[System]") {
    // Declared first so it outlives the manager and the systems it releases.
    TimerLog log;

    SECTION("during a container pass") {
        ecs::Manager manager;
        auto container = manager.Container("test-container");
        auto s = timerSystemAdd(container, log, "S");
        auto t = timerSystemAdd(container, log, "T");
        int destroyedDuringT = -1;
        t->onUpdate = [&] { destroyedDuringT = log.count("destroyed:S"); };

        timerAdd(s, "t1", [&] {
            log.counts["fire:t1"]++;
            s->Container->SystemDestroy(s->Handle);
            // The system must still be readable after it has been removed.
            log.counts["handle-size"] = (int)s->Handle.size();
        });
        timerAdd(s, "t2", fireCounter(log, "t2"));

        container->Update();
        REQUIRE(log.count("fire:t1") == 1);
        REQUIRE(log.count("fire:t2") == 0);
        REQUIRE(log.count("handle-size") == (int)timerHandle("S").size());
        REQUIRE(log.count("update:S") == 0);
        REQUIRE(log.count("update:T") == 1);
        REQUIRE(destroyedDuringT == 0);
        REQUIRE(log.count("destroyed:S") == 1);
        REQUIRE_FALSE(container->Systems.contains(timerHandle("S")));

        container->Update();
        REQUIRE(log.count("fire:t1") == 1);
        REQUIRE(log.count("update:T") == 2);
        REQUIRE(log.count("destroyed:S") == 1);
    }

    SECTION("when the system is updated directly") {
        {
            ecs::Manager manager;
            auto container = manager.Container("test-container");
            auto s = timerSystemAdd(container, log, "S");
            timerAdd(s, "t1", [&] {
                log.counts["fire:t1"]++;
                s->Container->SystemDestroy(s->Handle);
                log.counts["handle-size"] = (int)s->Handle.size();
            });
            timerAdd(s, "t2", fireCounter(log, "t2"));

            s->UpdateSystem();
            REQUIRE(log.count("fire:t1") == 1);
            REQUIRE(log.count("fire:t2") == 0);
            REQUIRE(log.count("handle-size") == (int)timerHandle("S").size());
            REQUIRE(log.count("update:S") == 0);
            REQUIRE_FALSE(container->Systems.contains(timerHandle("S")));
            REQUIRE(log.count("destroyed:S") <= 1);

            // Released at the end of the next pass at the latest.
            container->Update();
            REQUIRE(log.count("destroyed:S") == 1);
        }
        REQUIRE(log.count("destroyed:S") == 1);
    }
}

TEST_CASE("A timer callback that changes timers and then throws", "[System]") {
    // Declared first so it outlives the manager and the systems it releases.
    TimerLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    container->LoggerSet([&](const std::string &message, const std::string &) {
        log.messages.push_back(message);
    });
    auto s = timerSystemAdd(container, log, "S");

    timerAdd(s, "A", [&] {
        log.counts["fire:A"]++;
        s->TimerAdd(ecs::Timer("C", fireCounter(log, "C"), std::chrono::microseconds(0)));
        s->TimerClear("B");
        throw std::runtime_error("deliberate failure");
    }, false);
    timerAdd(s, "B", fireCounter(log, "B"));
    timerAdd(s, "D", fireCounter(log, "D"));

    REQUIRE_THROWS(container->Update());

    bool logged = false;
    for(const auto &message : log.messages)
    {
        if(message.find(timerHandle("S")) != std::string::npos) logged = true;
    }
    REQUIRE(logged);
    REQUIRE(log.count("fire:A") == 1);
    REQUIRE(log.count("fire:B") == 0);
    REQUIRE(log.count("fire:D") == 0);
    REQUIRE(log.count("update:S") == 0);
    REQUIRE(s->TimerCount("A") == 0);
    REQUIRE(s->TimerCount("B") == 0);
    REQUIRE(s->TimerCount("C") == 1);
    REQUIRE(s->TimerCount("D") == 1);

    container->Update();
    REQUIRE(log.count("fire:A") == 1);
    REQUIRE(log.count("fire:B") == 0);
    REQUIRE(log.count("fire:C") == 1);
    REQUIRE(log.count("fire:D") == 1);
    REQUIRE(log.count("update:S") == 1);
}
