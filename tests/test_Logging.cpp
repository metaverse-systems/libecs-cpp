#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Logging behaviour, observed through a recording destination installed with LoggerSet().

namespace
{
    // Collects every (message, level) pair a world delivers to its destination.
    struct Recorder
    {
        using Line = std::pair<std::string, std::string>;

        void record(const std::string &message, const std::string &level)
        {
            std::lock_guard<std::mutex> lock(this->lock);
            this->lines.emplace_back(message, level);
        }

        std::vector<Line> snapshot() const
        {
            std::lock_guard<std::mutex> lock(this->lock);
            return this->lines;
        }

        size_t count() const { return this->snapshot().size(); }

        bool contains(const std::string &message, const std::string &level) const
        {
            const auto all = this->snapshot();
            return std::find(all.begin(), all.end(), Line(message, level)) != all.end();
        }

        // Installs the recorder as the destination of the world. The recorder must outlive the world's use of it.
        void install(ecs::Container *world)
        {
            world->LoggerSet([this](const std::string &message, const std::string &level) { this->record(message, level); });
        }

        mutable std::mutex lock;
        std::vector<Line> lines;
    };

    // A system with a fixed handle that logs on request.
    class LoggingSystem : public ecs::System
    {
      public:
        explicit LoggingSystem(std::string handle) { this->Handle = std::move(handle); }

        nlohmann::json Export() const { return nlohmann::json::object(); }
    };

    // Logs from Shutdown() and from its destructor.
    class TeardownSystem : public ecs::System
    {
      public:
        explicit TeardownSystem(std::string handle) { this->Handle = std::move(handle); }
        ~TeardownSystem() { this->Log("destroyed", "info"); }

        nlohmann::json Export() const { return nlohmann::json::object(); }

        void Update() { this->updates.fetch_add(1); }
        void Shutdown() { this->Log("shutdown", "warning"); }

        std::atomic<int> updates{0};
    };
}

TEST_CASE("Container::Log delivers the message and the level unchanged", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);

    world->Log("hello", "warning");
    world->Log("second", "error");

    const std::vector<Recorder::Line> expected = {{"hello", "warning"}, {"second", "error"}};
    REQUIRE(recorder.snapshot() == expected);
}

TEST_CASE("Container::Log with no severity delivers info", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);

    world->Log("plain");

    const std::vector<Recorder::Line> expected = {{"plain", "info"}};
    REQUIRE(recorder.snapshot() == expected);
}

TEST_CASE("A replacement destination replaces the previous one", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder first;
    Recorder second;
    first.install(world);
    world->Log("to-first");
    second.install(world);
    world->Log("to-second");

    REQUIRE(first.snapshot() == std::vector<Recorder::Line>({{"to-first", "info"}}));
    REQUIRE(second.snapshot() == std::vector<Recorder::Line>({{"to-second", "info"}}));
}

TEST_CASE("An empty destination drops lines without throwing", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);
    world->Log("kept");

    world->LoggerSet(nullptr);
    REQUIRE_NOTHROW(world->Log("dropped"));
    REQUIRE_NOTHROW(world->Log("dropped too", "error"));

    // A system logging through an empty destination is dropped as well.
    auto system = std::make_unique<LoggingSystem>("quiet");
    auto *raw = world->System(std::move(system));
    REQUIRE_NOTHROW(raw->Log("also dropped", "info"));

    REQUIRE(recorder.snapshot() == std::vector<Recorder::Line>({{"kept", "info"}}));
}

TEST_CASE("A system attached to a world logs with its handle as prefix and its severity", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);
    auto *system = world->System(std::make_unique<LoggingSystem>("speaker"));

    system->Log("first", "warning");
    system->Log("second", "error");
    system->Log("third", "debug");

    const std::vector<Recorder::Line> expected = {
        {"[speaker] first", "warning"}, {"[speaker] second", "error"}, {"[speaker] third", "debug"}};
    REQUIRE(recorder.snapshot() == expected);
}

TEST_CASE("A destination may call Log and LoggerSet without deadlock", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    bool inside = false;

    world->LoggerSet([&](const std::string &message, const std::string &level) {
        recorder.record(message, level);
        if(inside) return;
        inside = true;
        // Logging from inside the destination, then replacing the destination from inside it.
        world->Log("nested", "debug");
        world->LoggerSet([&](const std::string &m, const std::string &l) { recorder.record("replacement:" + m, l); });
        world->Log("after-replace", "info");
    });

    world->Log("outer", "info");

    // The nested call runs the outer destination again, which records the nested line; the line
    // logged after the replacement reaches the replacement.
    const auto lines = recorder.snapshot();
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0] == Recorder::Line("outer", "info"));
    REQUIRE(lines[1] == Recorder::Line("nested", "debug"));
    REQUIRE(lines[2] == Recorder::Line("replacement:after-replace", "info"));
}

TEST_CASE("A destination receives plain text with no escape sequence for every severity", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);
    auto *system = world->System(std::make_unique<LoggingSystem>("plain"));

    const std::vector<std::string> levels = {"info", "warning", "error", "debug", "trace", "critical", "unknown-level", ""};
    for(const auto &level : levels)
    {
        world->Log("world line", level);
        system->Log("system line", level);
    }

    const auto lines = recorder.snapshot();
    REQUIRE(lines.size() == levels.size() * 2);
    for(const auto &[message, level] : lines)
    {
        INFO("message: " << message << " level: " << level);
        REQUIRE(message.find('\033') == std::string::npos);
        REQUIRE(level.find('\033') == std::string::npos);
    }
    // The text arrives exactly as given.
    REQUIRE(lines[0] == Recorder::Line("world line", "info"));
    REQUIRE(lines[1] == Recorder::Line("[plain] system line", "info"));
}

TEST_CASE("A system that logs from Shutdown and its destructor reaches the destination on a stop", "[Logging]") {
    auto recorder = std::make_shared<Recorder>();
    {
        ecs::Manager manager;
        auto world = manager.Container("logging");
        recorder->install(world);
        world->System(std::make_unique<TeardownSystem>("teardown"));
        world->Update();
        world->Stop();
        REQUIRE(recorder->contains("[teardown] shutdown", "warning"));
    }
    // After the world is released the destructor's line has arrived too.
    REQUIRE(recorder->contains("[teardown] destroyed", "info"));
    REQUIRE(recorder->count() == 2);
}

TEST_CASE("A system that logs from Shutdown and its destructor reaches the destination when it is removed", "[Logging]") {
    Recorder recorder;
    ecs::Manager manager;
    auto world = manager.Container("logging");
    recorder.install(world);
    world->System(std::make_unique<TeardownSystem>("teardown"));
    world->Update();

    world->SystemDestroy("teardown");

    REQUIRE(recorder.contains("[teardown] shutdown", "warning"));
    REQUIRE(recorder.contains("[teardown] destroyed", "info"));
    REQUIRE(recorder.count() == 2);
}

TEST_CASE("A system that logs from Shutdown and its destructor reaches the destination when the manager shuts down", "[Logging]") {
    Recorder recorder;
    ecs::Manager manager;
    auto world = manager.Container("logging");
    recorder.install(world);
    auto owned = std::make_unique<TeardownSystem>("teardown");
    owned->Timing.SetInterval(std::chrono::microseconds(0));
    TeardownSystem *system = owned.get();
    world->System(std::move(owned));
    world->Start(1000);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while(system->updates.load() < 1 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    REQUIRE(system->updates.load() >= 1);

    manager.Shutdown();

    REQUIRE(recorder.contains("[teardown] shutdown", "warning"));
}

TEST_CASE("A system that logs from its destructor reaches the destination when the world is destroyed", "[Logging]") {
    auto recorder = std::make_shared<Recorder>();
    {
        ecs::Manager manager;
        auto world = manager.Container("logging");
        recorder->install(world);
        world->System(std::make_unique<TeardownSystem>("teardown"));
        world->Update();
    }

    REQUIRE(recorder->contains("[teardown] shutdown", "warning"));
    REQUIRE(recorder->contains("[teardown] destroyed", "info"));
}

TEST_CASE("A system logging without a severity delivers info", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);
    auto *system = world->System(std::make_unique<LoggingSystem>("speaker"));

    system->Log("m");

    const std::vector<Recorder::Line> expected = {{"[speaker] m", "info"}};
    REQUIRE(recorder.snapshot() == expected);
}

TEST_CASE("A world logging without a severity delivers info", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);

    world->Log("m");

    const std::vector<Recorder::Line> expected = {{"m", "info"}};
    REQUIRE(recorder.snapshot() == expected);
}

TEST_CASE("A system and its world deliver the same severity for the same call", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);
    auto *system = world->System(std::make_unique<LoggingSystem>("speaker"));

    world->Log("m");
    system->Log("m");

    const auto lines = recorder.snapshot();
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0].second == lines[1].second);
    REQUIRE(lines[0].second == "info");
}

TEST_CASE("An explicit severity is delivered as given", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);
    auto *system = world->System(std::make_unique<LoggingSystem>("speaker"));

    const std::vector<std::string> levels = {"error", "warning", "debug", "info", "no-such-severity"};
    for(const auto &level : levels)
    {
        system->Log("from system", level);
        world->Log("from world", level);
    }

    const auto lines = recorder.snapshot();
    REQUIRE(lines.size() == levels.size() * 2);
    for(size_t i = 0; i < levels.size(); ++i)
    {
        INFO("level: " << levels[i]);
        REQUIRE(lines[i * 2] == Recorder::Line("[speaker] from system", levels[i]));
        REQUIRE(lines[i * 2 + 1] == Recorder::Line("from world", levels[i]));
    }
}

TEST_CASE("A call written with the severity as a second argument still behaves the same", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);
    auto *system = world->System(std::make_unique<LoggingSystem>("speaker"));

    system->Log("m", "warning");

    const std::vector<Recorder::Line> expected = {{"[speaker] m", "warning"}};
    REQUIRE(recorder.snapshot() == expected);
}
