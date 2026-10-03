#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>
#include "../src/ConsoleLog.hpp"

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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
        explicit LoggingSystem(std::string handle) : ecs::System(std::move(handle)) {}

        nlohmann::json Export() const { return nlohmann::json::object(); }
    };

    // Large loops run shorter when a sanitizer slows the program down.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    constexpr int kSanitizerFactor = 4;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
    constexpr int kSanitizerFactor = 4;
#else
    constexpr int kSanitizerFactor = 1;
#endif
#else
    constexpr int kSanitizerFactor = 1;
#endif

    using HeldLines = std::vector<std::pair<std::string, std::string>>;

    // Logs the given lines from its constructor, before any world exists, and never logs again.
    class HeldSystem : public ecs::System
    {
      public:
        HeldSystem(const std::string &handle, const HeldLines &held) : ecs::System(handle)
        {
            for(const auto &[message, level] : held) this->Log(message, level);
        }

        nlohmann::json Export() const { return nlohmann::json::object(); }
    };

    // Redirects std::cout and std::cerr into buffers for the life of the object.
    struct ConsoleCapture
    {
        ConsoleCapture() : coutBefore(std::cout.rdbuf(this->out.rdbuf())), cerrBefore(std::cerr.rdbuf(this->err.rdbuf())) {}
        ~ConsoleCapture()
        {
            std::cout.rdbuf(this->coutBefore);
            std::cerr.rdbuf(this->cerrBefore);
        }

        std::ostringstream out;
        std::ostringstream err;
        std::streambuf *coutBefore;
        std::streambuf *cerrBefore;
    };

    // Exposes the protected componentsClear() and logs on request.
    class ClearingSystem : public ecs::System
    {
      public:
        explicit ClearingSystem(const std::string &handle) : ecs::System(handle) {}

        nlohmann::json Export() const { return nlohmann::json::object(); }

        void clear() { this->componentsClear(); }
    };

    // Logs from Shutdown() and from its destructor.
    class TeardownSystem : public ecs::System
    {
      public:
        explicit TeardownSystem(std::string handle) : ecs::System(std::move(handle)) {}
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

TEST_CASE("Lines logged before attachment are delivered at registration, in order, without another Log call", "[Logging]") {
    // A system that logs only while it is being built must still be heard once it joins a world.
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);

    world->System(std::make_unique<HeldSystem>("early", HeldLines{{"first", "warning"}, {"second", "error"}}));

    const std::vector<Recorder::Line> expected = {{"[early] first", "warning"}, {"[early] second", "error"}};
    REQUIRE(recorder.snapshot() == expected);
}

TEST_CASE("Held lines are delivered exactly once", "[Logging]") {
    // Registering delivers the held lines and a later Log call must not send them again.
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);

    auto *system = world->System(std::make_unique<HeldSystem>("early", HeldLines{{"held one", "info"}, {"held two", "debug"}}));
    REQUIRE(recorder.count() == 2);
    system->Log("later", "warning");
    system->Log("latest");

    const std::vector<Recorder::Line> expected = {
        {"[early] held one", "info"}, {"[early] held two", "debug"}, {"[early] later", "warning"}, {"[early] latest", "info"}};
    REQUIRE(recorder.snapshot() == expected);
}

TEST_CASE("Registering a system that held nothing delivers nothing", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);

    world->System(std::make_unique<HeldSystem>("silent", HeldLines{}));
    world->System(std::make_unique<LoggingSystem>("also-silent"));

    REQUIRE(recorder.count() == 0);
}

TEST_CASE("A refused registration delivers nothing", "[Logging]") {
    // None of the refusals leaves an unattached object behind: the empty-identifier system is destroyed
    // with the failed call and the already attached one stays with the world that owns it.
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);

    SECTION("empty identifier") {
        REQUIRE_THROWS_AS(world->System(std::make_unique<HeldSystem>("", HeldLines{{"held", "info"}})), std::runtime_error);
        REQUIRE(recorder.count() == 0);
    }

    SECTION("missing system") {
        REQUIRE_THROWS_AS(world->System(std::unique_ptr<ecs::System>()), std::runtime_error);
        REQUIRE(recorder.count() == 0);
    }

    SECTION("system already attached to another world") {
        auto other = manager.Container("other");
        Recorder otherRecorder;
        otherRecorder.install(other);
        auto *system = world->System(std::make_unique<HeldSystem>("early", HeldLines{{"held", "info"}}));
        const std::vector<Recorder::Line> expected = {{"[early] held", "info"}};
        REQUIRE(recorder.snapshot() == expected);

        // The pointer aliases an object the first world owns; the refusal leaves it there.
        REQUIRE_THROWS_AS(other->System(std::unique_ptr<ecs::System>(system)), std::runtime_error);

        REQUIRE(otherRecorder.count() == 0);
        REQUIRE(recorder.snapshot() == expected);
    }
}

TEST_CASE("Held lines carry the identifier the system has at registration", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);

    world->System(std::make_unique<HeldSystem>("registered-as", HeldLines{{"m", "info"}}));

    const std::vector<Recorder::Line> expected = {{"[registered-as] m", "info"}};
    REQUIRE(recorder.snapshot() == expected);
}

TEST_CASE("A system that replaces another delivers its held lines once and leaves the replaced lines alone", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);
    world->System(std::make_unique<HeldSystem>("same", HeldLines{{"old held", "info"}}));
    REQUIRE(recorder.count() == 1);

    auto *replacement = world->System(std::make_unique<HeldSystem>("same", HeldLines{{"new held", "warning"}}));
    replacement->Log("new later");

    const std::vector<Recorder::Line> expected = {
        {"[same] old held", "info"}, {"[same] new held", "warning"}, {"[same] new later", "info"}};
    REQUIRE(recorder.snapshot() == expected);
}

TEST_CASE("A large batch of held lines arrives complete and in order", "[Logging]") {
    // At least 1000 lines even when a sanitizer shortens the loop.
    const int total = 4000 / kSanitizerFactor;
    HeldLines held;
    for(int i = 0; i < total; ++i) held.emplace_back("line " + std::to_string(i), i % 2 == 0 ? "info" : "warning");

    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);

    world->System(std::make_unique<HeldSystem>("batch", held));

    const auto lines = recorder.snapshot();
    REQUIRE(lines.size() == static_cast<size_t>(total));
    for(int i = 0; i < total; ++i)
    {
        if(lines[i] != Recorder::Line("[batch] line " + std::to_string(i), i % 2 == 0 ? "info" : "warning"))
        {
            FAIL("line " << i << " is missing or out of order: " << lines[i].first);
        }
    }
}

TEST_CASE("A destination that throws on the first held line does not stop the rest or the registration", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    world->LoggerSet([&](const std::string &message, const std::string &level) {
        const bool first = recorder.count() == 0;
        recorder.record(message, level);
        if(first) throw std::runtime_error("destination failure");
    });

    ecs::System *system = nullptr;
    REQUIRE_NOTHROW(system = world->System(std::make_unique<HeldSystem>(
                        "early", HeldLines{{"one", "info"}, {"two", "warning"}, {"three", "error"}})));

    const std::vector<Recorder::Line> expected = {{"[early] one", "info"}, {"[early] two", "warning"}, {"[early] three", "error"}};
    REQUIRE(recorder.snapshot() == expected);
    // The registration stands: the system is in the world and logs normally.
    REQUIRE(world->Systems.contains("early"));
    system->Log("after");
    REQUIRE(recorder.contains("[early] after", "info"));
}

TEST_CASE("A destination that replaces itself during delivery hands each remaining line to the current destination", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder first;
    Recorder second;
    world->LoggerSet([&](const std::string &message, const std::string &level) {
        first.record(message, level);
        second.install(world);
    });

    world->System(std::make_unique<HeldSystem>("early", HeldLines{{"one", "info"}, {"two", "warning"}, {"three", "error"}}));

    REQUIRE(first.snapshot() == std::vector<Recorder::Line>({{"[early] one", "info"}}));
    REQUIRE(second.snapshot() == std::vector<Recorder::Line>({{"[early] two", "warning"}, {"[early] three", "error"}}));
}

TEST_CASE("A destination that logs back through the system sees no repeated line and no recursion", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    ecs::System *system = nullptr;
    int depth = 0;
    int deepest = 0;
    bool echoed = false;
    world->LoggerSet([&](const std::string &message, const std::string &level) {
        depth++;
        deepest = std::max(deepest, depth);
        recorder.record(message, level);
        if(!echoed && system != nullptr)
        {
            echoed = true;
            system->Log("echo", "debug");
        }
        depth--;
    });

    auto owned = std::make_unique<HeldSystem>("early", HeldLines{{"one", "info"}, {"two", "warning"}});
    system = owned.get();
    world->System(std::move(owned));

    const auto lines = recorder.snapshot();
    REQUIRE(std::count(lines.begin(), lines.end(), Recorder::Line("[early] one", "info")) == 1);
    REQUIRE(std::count(lines.begin(), lines.end(), Recorder::Line("[early] two", "warning")) == 1);
    REQUIRE(std::count(lines.begin(), lines.end(), Recorder::Line("[early] echo", "debug")) == 1);
    REQUIRE(lines.size() == 3);
    // The held lines keep their order relative to each other.
    const auto one = std::find(lines.begin(), lines.end(), Recorder::Line("[early] one", "info"));
    const auto two = std::find(lines.begin(), lines.end(), Recorder::Line("[early] two", "warning"));
    REQUIRE(one < two);
    // The echo is delivered from inside the destination once; it is not delivered again from inside itself.
    REQUIRE(deepest <= 2);
}

TEST_CASE("A system destroyed without being registered delivers nothing and writes nothing", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);
    ConsoleCapture console;

    {
        HeldSystem lonely("lonely", HeldLines{{"never heard", "error"}});
        lonely.Log("also never heard");
    }

    REQUIRE(recorder.count() == 0);
    REQUIRE(console.out.str().empty());
    REQUIRE(console.err.str().empty());
}

TEST_CASE("Another thread logging and replacing the destination during registrations loses no held line", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);

    std::atomic<bool> stop{false};
    std::thread noise([&]() {
        while(!stop.load())
        {
            world->Log("noise");
            recorder.install(world);
            std::this_thread::yield();
        }
    });

    const int systems = 20 / kSanitizerFactor + 1;
    const int linesEach = 50;
    for(int s = 0; s < systems; ++s)
    {
        HeldLines held;
        for(int i = 0; i < linesEach; ++i) held.emplace_back("m" + std::to_string(i), "info");
        world->System(std::make_unique<HeldSystem>("sys" + std::to_string(s), held));
    }

    stop.store(true);
    noise.join();

    const auto lines = recorder.snapshot();
    for(int s = 0; s < systems; ++s)
    {
        const std::string prefix = "[sys" + std::to_string(s) + "] ";
        // Each system's lines arrive once and in order among themselves.
        int next = 0;
        for(const auto &[message, level] : lines)
        {
            if(message.rfind(prefix, 0) != 0) continue;
            REQUIRE(message == prefix + "m" + std::to_string(next));
            next++;
        }
        REQUIRE(next == linesEach);
    }
}

TEST_CASE("componentsClear on a system with no world writes nothing to the console and holds its warning", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);
    ConsoleCapture console;

    auto system = std::make_unique<ClearingSystem>("clearing");
    system->clear();

    REQUIRE(console.out.str().empty());
    REQUIRE(console.err.str().empty());
    // Held, not delivered: the system has no world to deliver to yet.
    REQUIRE(recorder.count() == 0);

    world->System(std::move(system));
    REQUIRE(recorder.contains("[clearing] Container is null in componentsClear()", "warning"));
    REQUIRE(console.out.str().empty());
    REQUIRE(console.err.str().empty());
}

TEST_CASE("The componentsClear warning is delivered at registration in order with the other held lines", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);

    auto system = std::make_unique<ClearingSystem>("clearing");
    system->Log("before", "info");
    system->clear();
    system->Log("after", "error");
    REQUIRE(recorder.count() == 0);

    world->System(std::move(system));

    const std::vector<Recorder::Line> expected = {{"[clearing] before", "info"},
                                                  {"[clearing] Container is null in componentsClear()", "warning"},
                                                  {"[clearing] after", "error"}};
    REQUIRE(recorder.snapshot() == expected);
}

TEST_CASE("componentsClear on a system attached to a world gives the destination no warning", "[Logging]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);
    ConsoleCapture console;

    auto *system = world->System(std::make_unique<ClearingSystem>("attached"));
    REQUIRE(recorder.count() == 0);
    static_cast<ClearingSystem *>(system)->clear();

    REQUIRE(recorder.count() == 0);
    REQUIRE(console.out.str().empty());
    REQUIRE(console.err.str().empty());
}

// The default destination: colour only on an interactive stream, decided per stream, over injected streams.

namespace
{
    const std::string escape = "\033";

    void setNoColour(const char *value)
    {
#ifdef _WIN32
        _putenv_s("NO_COLOR", value);
#else
        setenv("NO_COLOR", value, 1);
#endif
    }

    void clearNoColour()
    {
#ifdef _WIN32
        _putenv_s("NO_COLOR", "");
#else
        unsetenv("NO_COLOR");
#endif
    }

    bool streamsAreTerminals()
    {
#ifdef _WIN32
        return _isatty(1) != 0 || _isatty(2) != 0;
#else
        return isatty(STDOUT_FILENO) != 0 || isatty(STDERR_FILENO) != 0;
#endif
    }

    // Restores NO_COLOR to what it was when the object is destroyed.
    class NoColourScope
    {
      public:
        NoColourScope()
        {
            const char *current = std::getenv("NO_COLOR");
            this->wasSet = current != nullptr;
            if(this->wasSet)
            {
                this->saved = current;
            }
        }

        ~NoColourScope()
        {
            if(this->wasSet)
            {
                setNoColour(this->saved.c_str());
            }
            else
            {
                clearNoColour();
            }
        }

        NoColourScope(const NoColourScope &) = delete;
        NoColourScope &operator=(const NoColourScope &) = delete;

      private:
        bool wasSet = false;
        std::string saved;
    };

    // Standard-library streams standing in for standard output and standard error.
    struct FakeStreams
    {
        std::ostringstream output;
        std::ostringstream error;

        ecs::Console console(bool colourOutput, bool colourError)
        {
            return ecs::Console{&this->output, &this->error, colourOutput, colourError};
        }
    };
}

TEST_CASE("A stream with colour on gets the colour code of the severity around the tag", "[Logging][Console]") {
    const std::vector<std::pair<std::string, std::string>> codes = {
        {"error", "91"}, {"warning", "93"}, {"debug", "97"}, {"info", "92"}, {"unknown", "92"}};

    for(const auto &[level, code] : codes)
    {
        DYNAMIC_SECTION("severity " << level)
        {
            FakeStreams streams;
            auto log = ecs::consoleLogger(streams.console(true, true));
            log("a message", level);

            const std::string expected = escape + "[" + code + "m[" + level + "]" + escape + "[0m a message\n";
            const bool toError = level == "error" || level == "warning";
            REQUIRE((toError ? streams.error.str() : streams.output.str()) == expected);
            REQUIRE((toError ? streams.output.str() : streams.error.str()).empty());
        }
    }
}

TEST_CASE("A stream with colour off gets the same text with no escape sequence", "[Logging][Console]") {
    for(const std::string level : {"error", "warning", "debug", "info", "unknown"})
    {
        DYNAMIC_SECTION("severity " << level)
        {
            FakeStreams streams;
            auto log = ecs::consoleLogger(streams.console(false, false));
            log("a message", level);

            const std::string expected = "[" + level + "] a message\n";
            const bool toError = level == "error" || level == "warning";
            REQUIRE((toError ? streams.error.str() : streams.output.str()) == expected);
            REQUIRE((toError ? streams.output.str() : streams.error.str()).empty());
            REQUIRE(streams.output.str().find(escape) == std::string::npos);
            REQUIRE(streams.error.str().find(escape) == std::string::npos);
        }
    }
}

TEST_CASE("Error and warning go to the error stream and the other severities to the output stream", "[Logging][Console]") {
    FakeStreams streams;
    auto log = ecs::consoleLogger(streams.console(false, false));

    log("one", "error");
    log("two", "warning");
    log("three", "info");
    log("four", "debug");
    log("five", "something else");

    REQUIRE(streams.error.str() == "[error] one\n[warning] two\n");
    REQUIRE(streams.output.str() == "[info] three\n[debug] four\n[something else] five\n");
}

TEST_CASE("The two stream flags are independent", "[Logging][Console]") {
    SECTION("output coloured, error clean")
    {
        FakeStreams streams;
        auto log = ecs::consoleLogger(streams.console(true, false));
        log("to output", "info");
        log("to error", "error");

        REQUIRE(streams.output.str() == escape + "[92m[info]" + escape + "[0m to output\n");
        REQUIRE(streams.error.str() == "[error] to error\n");
    }
    SECTION("output clean, error coloured")
    {
        FakeStreams streams;
        auto log = ecs::consoleLogger(streams.console(false, true));
        log("to output", "info");
        log("to error", "error");

        REQUIRE(streams.output.str() == "[info] to output\n");
        REQUIRE(streams.error.str() == escape + "[91m[error]" + escape + "[0m to error\n");
    }
}

TEST_CASE("An unknown severity is treated like info", "[Logging][Console]") {
    FakeStreams known;
    FakeStreams unknown;
    ecs::consoleLogger(known.console(true, true))("same", "info");
    ecs::consoleLogger(unknown.console(true, true))("same", "no-such-severity");

    REQUIRE(known.error.str().empty());
    REQUIRE(unknown.error.str().empty());
    REQUIRE(unknown.output.str() == escape + "[92m[no-such-severity]" + escape + "[0m same\n");
    REQUIRE(known.output.str() == escape + "[92m[info]" + escape + "[0m same\n");
}

TEST_CASE("Colour is wanted only on a terminal with NO_COLOR not set to a value", "[Logging][Console]") {
    REQUIRE(ecs::colourWanted(true, false));
    REQUIRE_FALSE(ecs::colourWanted(true, true));
    REQUIRE_FALSE(ecs::colourWanted(false, false));
    REQUIRE_FALSE(ecs::colourWanted(false, true));
}

TEST_CASE("The environment decision is made once per stream and reads NO_COLOR as set only when non-empty", "[Logging][Console]") {
    NoColourScope scope;

    // Neither standard stream is a terminal under the test runner, so the flags are false whatever NO_COLOR says.
    if(streamsAreTerminals())
    {
        SKIP("standard output or standard error is a terminal; run with both redirected");
    }

    SECTION("NO_COLOR unset")
    {
        clearNoColour();
        const ecs::Console console = ecs::Console::fromEnvironment();
        REQUIRE_FALSE(console.colourOutput);
        REQUIRE_FALSE(console.colourError);
    }
    SECTION("NO_COLOR set to a non-empty value")
    {
        setNoColour("1");
        const ecs::Console console = ecs::Console::fromEnvironment();
        REQUIRE_FALSE(console.colourOutput);
        REQUIRE_FALSE(console.colourError);
    }
    SECTION("NO_COLOR set to an empty value")
    {
        setNoColour("");
        const ecs::Console console = ecs::Console::fromEnvironment();
        REQUIRE_FALSE(console.colourOutput);
        REQUIRE_FALSE(console.colourError);
    }
}

TEST_CASE("A destination replaced with a recording one receives plain text for every severity", "[Logging][Console]") {
    ecs::Manager manager;
    auto world = manager.Container("console");
    Recorder recorder;
    recorder.install(world);

    for(const std::string level : {"error", "warning", "debug", "info", "unknown"})
    {
        world->Log("text of " + level, level);
    }

    const auto lines = recorder.snapshot();
    REQUIRE(lines.size() == 5);
    for(const auto &[message, level] : lines)
    {
        REQUIRE(message == "text of " + level);
        REQUIRE(message.find(escape) == std::string::npos);
        REQUIRE(level.find(escape) == std::string::npos);
    }
}

// A system's identifier is fixed by its constructor, so it is the one it logs with and is exported under.

static_assert(!std::is_assignable_v<decltype((std::declval<ecs::System &>().Handle)), const char *>,
  "a system's identifier cannot be assigned after construction");

TEST_CASE("Held lines use the identifier given to the constructor", "[Logging][Identifier]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);

    world->System(std::make_unique<HeldSystem>("from-constructor", HeldLines{{"a", "info"}, {"b", "warning"}}));

    const std::vector<Recorder::Line> expected = {{"[from-constructor] a", "info"}, {"[from-constructor] b", "warning"}};
    REQUIRE(recorder.snapshot() == expected);
}

TEST_CASE("A registered system logs and is exported under its construction-time identifier", "[Logging][Identifier]") {
    ecs::Manager manager;
    auto world = manager.Container("logging");
    Recorder recorder;
    recorder.install(world);

    auto *system = world->System(std::make_unique<LoggingSystem>("fixed"));
    system->Log("later", "error");

    const std::vector<Recorder::Line> expected = {{"[fixed] later", "error"}};
    REQUIRE(recorder.snapshot() == expected);
    REQUIRE(system->Handle == "fixed");
    REQUIRE(world->Export()["Systems"].contains("fixed"));
}
