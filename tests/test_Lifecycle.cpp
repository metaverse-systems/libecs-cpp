#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Cases that cover when a system is started, shut down and stopped. Everything here uses only the public API.

namespace
{
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

    // Set when a watchdog expires so workers that poll it can leave their loops.
    std::atomic<bool> watchdogExpired{false};

    // Runs fn on a worker thread and waits for it. When the time is up the shared stop flag is set, the
    // worker is joined (it is never detached) and the test fails.
    template <typename Fn>
    auto withTimeout(Fn fn, std::chrono::seconds limit) -> decltype(fn())
    {
        using Result = decltype(fn());
        std::packaged_task<Result()> task(std::move(fn));
        std::future<Result> future = task.get_future();
        watchdogExpired.store(false);
        std::thread worker([&task] { task(); });
        const bool finished = future.wait_for(limit) == std::future_status::ready;
        if(!finished)
        {
            watchdogExpired.store(true);
        }
        worker.join();
        if(!finished)
        {
            FAIL("timed out after " << limit.count() << " s");
        }
        return future.get();
    }

    bool waitUntil(const std::function<bool()> &predicate, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while(!predicate())
        {
            if(std::chrono::steady_clock::now() >= deadline || watchdogExpired.load()) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    struct Event
    {
        long sequence;
        std::string handle;
        std::string event;
        std::thread::id thread;
    };

    // Ordered record of what the systems were asked to do, shared by every system of a test.
    class EventLog
    {
      public:
        long add(const std::string &handle, const std::string &event)
        {
            std::lock_guard<std::mutex> guard(this->lock);
            const long sequence = this->next++;
            this->events.push_back(Event{sequence, handle, event, std::this_thread::get_id()});
            return sequence;
        }

        std::vector<Event> snapshot()
        {
            std::lock_guard<std::mutex> guard(this->lock);
            return this->events;
        }

        // Handles of the systems that logged this event, in the order it happened.
        std::vector<std::string> handlesOf(const std::string &event)
        {
            std::vector<std::string> out;
            for(const auto &e : this->snapshot())
            {
                if(e.event == event) out.push_back(e.handle);
            }
            return out;
        }

      private:
        std::mutex lock;
        long next = 0;
        std::vector<Event> events;
    };

    // What one system was asked to do. Held by shared pointer so it outlives the system.
    struct Counters
    {
        std::atomic<int> initialize{0};
        std::atomic<int> update{0};
        std::atomic<int> shutdown{0};
        std::atomic<long> startSequence{-1};
        std::atomic<long> firstUpdateSequence{-1};
        std::atomic<long> shutdownSequence{-1};

        std::thread::id initializeThread() { std::lock_guard<std::mutex> g(this->lock); return this->initThread; }
        std::thread::id updateThread() { std::lock_guard<std::mutex> g(this->lock); return this->updThread; }
        std::thread::id shutdownThread() { std::lock_guard<std::mutex> g(this->lock); return this->downThread; }

        void noteInitialize() { std::lock_guard<std::mutex> g(this->lock); this->initThread = std::this_thread::get_id(); }
        void noteUpdate() { std::lock_guard<std::mutex> g(this->lock); this->updThread = std::this_thread::get_id(); }
        void noteShutdown() { std::lock_guard<std::mutex> g(this->lock); this->downThread = std::this_thread::get_id(); }

      private:
        std::mutex lock;
        std::thread::id initThread;
        std::thread::id updThread;
        std::thread::id downThread;
    };

    class CountingSystem : public ecs::System
    {
      public:
        CountingSystem(EventLog *log, const std::string &handle)
            : ecs::System(handle), log(log)
        {
            // Every pass updates this system.
            this->Timing.SetFrequency(0);
        }

        ~CountingSystem()
        {
            if(this->logInHooks) this->Log("destroying " + this->Handle, "info");
        }

        nlohmann::json Export() const
        {
            nlohmann::json config;
            config["Handle"] = this->Handle;
            return config;
        }

        void Initialize()
        {
            this->counters->noteInitialize();
            const long sequence = this->record("initialize");
            long expected = -1;
            this->counters->startSequence.compare_exchange_strong(expected, sequence);
            this->counters->initialize++;
            if(this->logInHooks) this->Log("initializing " + this->Handle, "info");
            if(this->onInitialize) this->onInitialize();
            if(this->throwInInitialize) throw std::runtime_error("initialize failed: " + this->Handle);
        }

        void Update()
        {
            this->counters->noteUpdate();
            const long sequence = this->record("update");
            long expected = -1;
            this->counters->firstUpdateSequence.compare_exchange_strong(expected, sequence);
            this->counters->update++;
            if(this->onUpdate) this->onUpdate();
            if(this->throwInUpdate) throw std::runtime_error("update failed: " + this->Handle);
        }

        void Shutdown()
        {
            this->counters->noteShutdown();
            this->counters->shutdownSequence.store(this->record("shutdown"));
            this->counters->shutdown++;
            if(this->logInHooks) this->Log("shutting down " + this->Handle, "info");
            if(this->onShutdown) this->onShutdown();
            if(this->throwInShutdown) throw std::runtime_error("shutdown failed: " + this->Handle);
        }

        std::shared_ptr<Counters> counters = std::make_shared<Counters>();
        bool throwInInitialize = false;
        bool throwInUpdate = false;
        bool throwInShutdown = false;
        bool logInHooks = false;
        std::function<void()> onInitialize;
        std::function<void()> onUpdate;
        std::function<void()> onShutdown;

      private:
        long record(const std::string &event)
        {
            return this->log ? this->log->add(this->Handle, event) : 0;
        }

        EventLog *log;
    };

    struct Subject
    {
        std::shared_ptr<Counters> counters;
        std::shared_ptr<std::unique_ptr<ecs::System>> holder;
    };

    // A system that has been built but not yet handed to a world.
    Subject makeSubject(EventLog &log, const std::string &handle)
    {
        auto system = std::make_unique<CountingSystem>(&log, handle);
        Subject subject;
        subject.counters = system->counters;
        subject.holder = std::make_shared<std::unique_ptr<ecs::System>>(std::move(system));
        return subject;
    }

    void registerSubject(ecs::Container *world, const Subject &subject)
    {
        world->System(std::move(*subject.holder));
    }

    void passes(ecs::Container *world, int count)
    {
        for(int i = 0; i < count; i++) world->Update();
    }

    // What the test saw of one system on its way in.
    struct Outcome
    {
        int starts = 0;
        int updates = 0;
        long startSequence = -1;
        long firstUpdateSequence = -1;
        std::thread::id initializeThread;
        std::thread::id updateThread;
        int startsAfterManyPasses = 0;
    };

    Outcome snapshotOf(const std::shared_ptr<Counters> &c)
    {
        Outcome o;
        o.starts = c->initialize.load();
        o.updates = c->update.load();
        o.startSequence = c->startSequence.load();
        o.firstUpdateSequence = c->firstUpdateSequence.load();
        o.initializeThread = c->initializeThread();
        o.updateThread = c->updateThread();
        return o;
    }

    // Caller-driven world: pass until the system has been updated (a few passes at most), then 1,000 more.
    Outcome observeCallerDriven(ecs::Container *world, const std::shared_ptr<Counters> &c)
    {
        for(int i = 0; i < 5 && c->update.load() < 1; i++) world->Update();
        Outcome o = snapshotOf(c);
        passes(world, 1000);
        o.startsAfterManyPasses = c->initialize.load();
        return o;
    }

    // Threaded world: wait for the first update, then for 1,000 more.
    Outcome observeThreaded(const std::shared_ptr<Counters> &c)
    {
        const auto limit = std::chrono::seconds(20 * kSanitizerFactor);
        waitUntil([&] { return c->update.load() >= 1; }, limit);
        Outcome o = snapshotOf(c);
        const int target = c->update.load() + 1000;
        waitUntil([&] { return c->update.load() >= target; }, limit);
        o.startsAfterManyPasses = c->initialize.load();
        return o;
    }

    struct ArrivalRow
    {
        std::string name;
        bool threaded;
        bool removedBeforeReached;
        std::function<Outcome(EventLog &)> run;
    };

    std::vector<ArrivalRow> arrivalRows()
    {
        std::vector<ArrivalRow> rows;

        rows.push_back({"registered before Start()", true, false, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("arrival");
            Subject subject = makeSubject(log, "subject");
            registerSubject(world, subject);
            world->Start(200);
            return observeThreaded(subject.counters);
        }});

        rows.push_back({"registered after Start() through Defer()", true, false, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("arrival");
            auto carrier = std::make_unique<CountingSystem>(&log, "carrier");
            auto carrierCounters = carrier->counters;
            world->System(std::move(carrier));
            world->Start(200);
            waitUntil([&] { return carrierCounters->update.load() >= 1; }, std::chrono::seconds(20 * kSanitizerFactor));
            Subject subject = makeSubject(log, "subject");
            world->Defer([world, subject] { registerSubject(world, subject); });
            return observeThreaded(subject.counters);
        }});

        rows.push_back({"caller-driven, before the first Update()", false, false, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("arrival");
            Subject subject = makeSubject(log, "subject");
            registerSubject(world, subject);
            return observeCallerDriven(world, subject.counters);
        }});

        rows.push_back({"caller-driven, after several Update() calls", false, false, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("arrival");
            world->System(std::make_unique<CountingSystem>(&log, "carrier"));
            passes(world, 5);
            Subject subject = makeSubject(log, "subject");
            registerSubject(world, subject);
            return observeCallerDriven(world, subject.counters);
        }});

        rows.push_back({"registered from inside another system's Initialize()", false, false, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("arrival");
            Subject subject = makeSubject(log, "subject");
            auto registrar = std::make_unique<CountingSystem>(&log, "registrar");
            registrar->onInitialize = [world, subject] { registerSubject(world, subject); };
            world->System(std::move(registrar));
            return observeCallerDriven(world, subject.counters);
        }});

        rows.push_back({"registered from inside another system's Update()", false, false, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("arrival");
            Subject subject = makeSubject(log, "subject");
            auto registrar = std::make_unique<CountingSystem>(&log, "registrar");
            auto done = std::make_shared<bool>(false);
            registrar->onUpdate = [world, subject, done] {
                if(*done) return;
                *done = true;
                registerSubject(world, subject);
            };
            world->System(std::move(registrar));
            return observeCallerDriven(world, subject.counters);
        }});

        rows.push_back({"registered as a replacement of a started system", false, false, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("arrival");
            world->System(std::make_unique<CountingSystem>(&log, "subject"));
            passes(world, 3);
            Subject replacement = makeSubject(log, "subject");
            registerSubject(world, replacement);
            return observeCallerDriven(world, replacement.counters);
        }});

        rows.push_back({"registered then removed before the next pass", false, true, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("arrival");
            world->System(std::make_unique<CountingSystem>(&log, "carrier"));
            passes(world, 1);
            Subject subject = makeSubject(log, "subject");
            registerSubject(world, subject);
            world->SystemDestroy("subject");
            passes(world, 3);
            return snapshotOf(subject.counters);
        }});

        rows.push_back({"registered and removed inside one pass", false, true, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("arrival");
            Subject subject = makeSubject(log, "subject");
            auto carrier = std::make_unique<CountingSystem>(&log, "carrier");
            auto done = std::make_shared<bool>(false);
            carrier->onUpdate = [world, subject, done] {
                if(*done) return;
                *done = true;
                registerSubject(world, subject);
                world->SystemDestroy("subject");
            };
            world->System(std::move(carrier));
            passes(world, 3);
            return snapshotOf(subject.counters);
        }});

        return rows;
    }
}

TEST_CASE("Arrival table: start-up exactly once before the first update", "[Lifecycle]")
{
    const auto table = arrivalRows();
    const auto row = GENERATE_COPY(from_range(table));

    DYNAMIC_SECTION(row.name)
    {
        EventLog log;
        Outcome o;
        if(row.threaded)
        {
            o = withTimeout([&] { return row.run(log); }, std::chrono::seconds(60 * kSanitizerFactor));
        }
        else
        {
            o = row.run(log);
        }

        if(row.removedBeforeReached)
        {
            CHECK(o.starts == 0);
            CHECK(o.updates == 0);
        }
        else
        {
            REQUIRE(o.starts == 1);
            REQUIRE(o.updates >= 1);
            REQUIRE(o.startSequence >= 0);
            CHECK(o.startSequence < o.firstUpdateSequence);
            CHECK(o.initializeThread == o.updateThread);
            if(row.threaded)
            {
                CHECK(o.initializeThread != std::this_thread::get_id());
            }
            else
            {
                CHECK(o.initializeThread == std::this_thread::get_id());
            }
            CHECK(o.startsAfterManyPasses == 1);
        }
    }
}

TEST_CASE("Start order within a step", "[Lifecycle]")
{
    EventLog log;
    ecs::Manager manager;
    auto *world = manager.Container("order");
    std::vector<std::shared_ptr<Counters>> counters;
    for(int i = 1; i <= 5; i++)
    {
        auto system = std::make_unique<CountingSystem>(&log, "s" + std::to_string(i));
        counters.push_back(system->counters);
        world->System(std::move(system));
    }

    world->Update();

    const std::vector<std::string> expected = {"s1", "s2", "s3", "s4", "s5"};
    CHECK(log.handlesOf("initialize") == expected);
    for(const auto &c : counters)
    {
        REQUIRE(c->initialize.load() == 1);
        REQUIRE(c->update.load() == 1);
        CHECK(c->startSequence.load() < c->firstUpdateSequence.load());
    }

    SECTION("a system registered during a pass is started by the next pass")
    {
        auto registrar = std::make_unique<CountingSystem>(&log, "registrar");
        Subject late = makeSubject(log, "late");
        auto done = std::make_shared<bool>(false);
        registrar->onUpdate = [world, late, done] {
            if(*done) return;
            *done = true;
            registerSubject(world, late);
        };
        world->System(std::move(registrar));

        world->Update();
        // Registered during the walk, so not part of that pass.
        CHECK(late.counters->update.load() == 0);

        world->Update();
        REQUIRE(late.counters->initialize.load() == 1);
        REQUIRE(late.counters->update.load() == 1);
        CHECK(late.counters->startSequence.load() < late.counters->firstUpdateSequence.load());

        passes(world, 100);
        CHECK(late.counters->initialize.load() == 1);
    }
}

TEST_CASE("SystemsInitialize then Update starts nothing twice", "[Lifecycle]")
{
    EventLog log;
    ecs::Manager manager;
    auto *world = manager.Container("explicit");
    auto first = std::make_unique<CountingSystem>(&log, "first");
    auto firstCounters = first->counters;
    world->System(std::move(first));

    SECTION("an explicit call followed by an update")
    {
        world->SystemsInitialize();
        REQUIRE(firstCounters->initialize.load() == 1);
        world->Update();
        passes(world, 10);
        CHECK(firstCounters->initialize.load() == 1);
        CHECK(firstCounters->update.load() == 11);
    }

    SECTION("an explicit call twice")
    {
        world->SystemsInitialize();
        world->SystemsInitialize();
        CHECK(firstCounters->initialize.load() == 1);
        world->Update();
        CHECK(firstCounters->initialize.load() == 1);
    }

    SECTION("a system registered after the explicit call is started by the next update")
    {
        world->SystemsInitialize();
        auto second = std::make_unique<CountingSystem>(&log, "second");
        auto secondCounters = second->counters;
        world->System(std::move(second));
        CHECK(secondCounters->initialize.load() == 0);

        world->Update();
        REQUIRE(secondCounters->initialize.load() == 1);
        REQUIRE(secondCounters->update.load() == 1);
        CHECK(secondCounters->startSequence.load() < secondCounters->firstUpdateSequence.load());
        CHECK(firstCounters->initialize.load() == 1);
    }
}

TEST_CASE("Failed start-up is not retried", "[Lifecycle]")
{
    EventLog log;
    ecs::Manager manager;
    auto *world = manager.Container("failing");
    std::vector<std::shared_ptr<Counters>> counters;
    for(int i = 1; i <= 5; i++)
    {
        auto system = std::make_unique<CountingSystem>(&log, "s" + std::to_string(i));
        if(i == 3) system->throwInInitialize = true;
        counters.push_back(system->counters);
        world->System(std::move(system));
    }

    REQUIRE_THROWS_AS(world->Update(), std::runtime_error);
    CHECK(counters[0]->initialize.load() == 1);
    CHECK(counters[1]->initialize.load() == 1);
    CHECK(counters[2]->initialize.load() == 1);

    REQUIRE_NOTHROW(world->Update());
    passes(world, 20);

    for(const auto &c : counters)
    {
        CHECK(c->initialize.load() == 1);
        CHECK(c->update.load() >= 1);
    }
    CHECK(counters[2]->update.load() >= 1);
    CHECK(log.handlesOf("initialize") == std::vector<std::string>{"s1", "s2", "s3", "s4", "s5"});
}

TEST_CASE("Removed before it was reached is not started", "[Lifecycle]")
{
    EventLog log;
    ecs::Manager manager;
    auto *world = manager.Container("removal");

    auto first = std::make_unique<CountingSystem>(&log, "first");
    first->onInitialize = [world] { world->SystemDestroy("second"); };
    auto firstCounters = first->counters;
    auto second = std::make_unique<CountingSystem>(&log, "second");
    auto secondCounters = second->counters;
    auto third = std::make_unique<CountingSystem>(&log, "third");
    auto thirdCounters = third->counters;
    world->System(std::move(first));
    world->System(std::move(second));
    world->System(std::move(third));

    passes(world, 5);

    CHECK(firstCounters->initialize.load() == 1);
    CHECK(thirdCounters->initialize.load() == 1);
    CHECK(secondCounters->initialize.load() == 0);
    CHECK(secondCounters->update.load() == 0);
    CHECK_FALSE(world->Systems.contains("second"));
}

TEST_CASE("Start-up of a late system runs on the world thread", "[Lifecycle]")
{
    struct Result
    {
        std::thread::id initializeThread;
        std::thread::id updateThread;
        std::thread::id carrierThread;
        int starts = 0;
    };

    const Result result = withTimeout([] {
        EventLog log;
        ecs::Manager manager;
        auto *world = manager.Container("late");
        auto carrier = std::make_unique<CountingSystem>(&log, "carrier");
        auto carrierCounters = carrier->counters;
        world->System(std::move(carrier));
        world->Start(200);
        const auto limit = std::chrono::seconds(20 * kSanitizerFactor);
        waitUntil([&] { return carrierCounters->update.load() >= 1; }, limit);

        Subject late = makeSubject(log, "late");
        world->Defer([world, late] { registerSubject(world, late); });
        waitUntil([&] { return late.counters->update.load() >= 1; }, limit);

        Result r;
        r.initializeThread = late.counters->initializeThread();
        r.updateThread = late.counters->updateThread();
        r.carrierThread = carrierCounters->updateThread();
        r.starts = late.counters->initialize.load();
        return r;
    }, std::chrono::seconds(60 * kSanitizerFactor));

    REQUIRE(result.starts == 1);
    CHECK(result.initializeThread != std::this_thread::get_id());
    CHECK(result.initializeThread == result.updateThread);
    CHECK(result.initializeThread == result.carrierThread);
}

namespace
{
    // Lines a world's log destination received, shared so it outlives the world.
    struct LogSink
    {
        void add(const std::string &message, const std::string &level)
        {
            std::lock_guard<std::mutex> guard(this->lock);
            this->lines.emplace_back(message, level);
        }

        std::vector<std::pair<std::string, std::string>> snapshot()
        {
            std::lock_guard<std::mutex> guard(this->lock);
            return this->lines;
        }

        // Index of the first line that contains the text, or -1.
        long indexOf(const std::string &text, const std::string &level = "")
        {
            const auto all = this->snapshot();
            for(size_t i = 0; i < all.size(); i++)
            {
                if(all[i].first.find(text) == std::string::npos) continue;
                if(!level.empty() && all[i].second != level) continue;
                return static_cast<long>(i);
            }
            return -1;
        }

        int count(const std::string &text)
        {
            int n = 0;
            for(const auto &line : this->snapshot())
            {
                if(line.first.find(text) != std::string::npos) n++;
            }
            return n;
        }

        bool throwOnError = false;

      private:
        std::mutex lock;
        std::vector<std::pair<std::string, std::string>> lines;
    };

    void sinkInstall(ecs::Container *world, const std::shared_ptr<LogSink> &sink)
    {
        world->LoggerSet([sink](const std::string &message, const std::string &level) {
            sink->add(message, level);
            if(sink->throwOnError && level == "error") throw std::runtime_error("log destination failed");
        });
    }

    std::vector<std::shared_ptr<Counters>> addSystems(ecs::Container *world, EventLog &log, int count)
    {
        std::vector<std::shared_ptr<Counters>> counters;
        for(int i = 1; i <= count; i++)
        {
            auto system = std::make_unique<CountingSystem>(&log, "s" + std::to_string(i));
            counters.push_back(system->counters);
            world->System(std::move(system));
        }
        return counters;
    }

    nlohmann::json messageTo(const std::string &system)
    {
        nlohmann::json message;
        message["destination"]["system"] = system;
        return message;
    }

    // Seen by a system from inside its own call to SystemDestroy().
    struct SelfProbe
    {
        int shutdownAtReturn = -1;
        int sum = 0;
    };

    // Removes itself from its second update, then reads a member it owns.
    class SelfRemover : public CountingSystem
    {
      public:
        SelfRemover(EventLog *log, const std::string &handle, std::shared_ptr<SelfProbe> probe)
            : CountingSystem(log, handle), probe(std::move(probe)) {}

        void Update()
        {
            CountingSystem::Update();
            if(this->counters->update.load() < 2 || this->done) return;
            this->done = true;
            const std::string handle = this->Handle;
            this->Container->SystemDestroy(handle);
            this->probe->shutdownAtReturn = this->counters->shutdown.load();
            for(int value : this->data) this->probe->sum += value;
        }

      private:
        std::shared_ptr<SelfProbe> probe;
        std::vector<int> data{1, 2, 3};
        bool done = false;
    };

    // What a departure row reports once its world is gone.
    struct Departure
    {
        std::vector<std::shared_ptr<Counters>> subjects;
        // Notifications must come last registered first.
        bool reverse = false;
        // True when the notifications must run on the thread that ran the updates.
        bool onWorldThread = false;
        std::vector<std::shared_ptr<Counters>> neverStarted;
    };

    struct DepartureRow
    {
        std::string name;
        bool threaded;
        std::function<Departure(EventLog &)> run;
    };

    std::vector<DepartureRow> departureRows()
    {
        std::vector<DepartureRow> rows;

        rows.push_back({"SystemDestroy() outside a pass", false, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("departure");
            Departure d;
            d.subjects = addSystems(world, log, 3);
            auto extra = std::make_unique<CountingSystem>(&log, "extra");
            d.neverStarted.push_back(extra->counters);
            passes(world, 2);
            world->System(std::move(extra));

            REQUIRE(world->Export().dump().find("s2") != std::string::npos);
            world->SystemDestroy("s2");
            CHECK(d.subjects[1]->shutdown.load() == 1);
            CHECK(d.subjects[1]->shutdownThread() == std::this_thread::get_id());
            CHECK_FALSE(world->Systems.contains("s2"));
            CHECK(world->Export().dump().find("s2") == std::string::npos);
            CHECK_THROWS_AS(world->MessageSubmit(messageTo("s2")), std::runtime_error);

            world->SystemDestroy("extra");
            passes(world, 2);
            CHECK(d.subjects[1]->shutdown.load() == 1);
            CHECK(d.subjects[1]->update.load() == 2);
            return d;
        }});

        rows.push_back({"SystemDestroy() from another system's Update()", false, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("departure");
            Departure d;
            auto remover = std::make_unique<CountingSystem>(&log, "remover");
            auto victim = std::make_unique<CountingSystem>(&log, "victim");
            auto witness = std::make_unique<CountingSystem>(&log, "witness");
            d.subjects = {remover->counters, victim->counters, witness->counters};
            auto victimCounters = victim->counters;
            struct Seen
            {
                int shutdownAtReturn = -1;
                bool inSystems = true;
                bool inExport = true;
                bool routed = true;
            };
            auto seen = std::make_shared<Seen>();
            auto done = std::make_shared<bool>(false);
            remover->onUpdate = [world, victimCounters, seen, done] {
                if(*done) return;
                *done = true;
                world->SystemDestroy("victim");
                seen->shutdownAtReturn = victimCounters->shutdown.load();
                seen->inSystems = world->Systems.contains("victim");
                seen->inExport = world->Export().dump().find("victim") != std::string::npos;
                try
                {
                    world->MessageSubmit(messageTo("victim"));
                }
                catch(const std::runtime_error &)
                {
                    seen->routed = false;
                }
            };
            world->System(std::move(remover));
            world->System(std::move(victim));
            world->System(std::move(witness));

            world->Update();
            // The call itself does not notify; the end of the pass does.
            CHECK(seen->shutdownAtReturn == 0);
            CHECK_FALSE(seen->inSystems);
            CHECK_FALSE(seen->inExport);
            CHECK_FALSE(seen->routed);
            CHECK(victimCounters->shutdown.load() == 1);
            CHECK(victimCounters->shutdownThread() == std::this_thread::get_id());
            CHECK(victimCounters->update.load() == 0);
            passes(world, 3);
            CHECK(victimCounters->shutdown.load() == 1);
            return d;
        }});

        rows.push_back({"SystemDestroy() of itself from its own Update()", false, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("departure");
            Departure d;
            auto probe = std::make_shared<SelfProbe>();
            auto selfie = std::make_unique<SelfRemover>(&log, "selfie", probe);
            d.subjects.push_back(selfie->counters);
            world->System(std::move(selfie));
            d.subjects.push_back(addSystems(world, log, 1)[0]);

            passes(world, 3);
            // What the system owns is still valid after the call, and the notification is not nested in it.
            CHECK(probe->sum == 6);
            CHECK(probe->shutdownAtReturn == 0);
            CHECK(d.subjects[0]->shutdown.load() == 1);
            CHECK(d.subjects[0]->update.load() == 2);
            CHECK_FALSE(world->Systems.contains("selfie"));
            return d;
        }});

        rows.push_back({"replacement of a started system", false, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("departure");
            Departure d;
            auto original = std::make_unique<CountingSystem>(&log, "subject");
            d.subjects.push_back(original->counters);
            world->System(std::move(original));
            passes(world, 2);

            auto replacement = std::make_unique<CountingSystem>(&log, "subject");
            d.subjects.push_back(replacement->counters);
            world->System(std::move(replacement));
            CHECK(d.subjects[0]->shutdown.load() == 1);
            CHECK(d.subjects[0]->shutdownThread() == std::this_thread::get_id());
            CHECK(d.subjects[1]->shutdown.load() == 0);
            passes(world, 2);
            CHECK(d.subjects[0]->shutdown.load() == 1);
            CHECK(d.subjects[0]->update.load() == 2);
            return d;
        }});

        rows.push_back({"caller-driven Stop()", false, [](EventLog &log) {
            ecs::Manager manager;
            auto *world = manager.Container("departure");
            Departure d;
            d.reverse = true;
            d.subjects = addSystems(world, log, 4);
            passes(world, 2);

            world->Stop();
            for(const auto &c : d.subjects)
            {
                CHECK(c->shutdown.load() == 1);
                CHECK(c->shutdownThread() == std::this_thread::get_id());
            }
            const int updates = d.subjects[0]->update.load();
            passes(world, 3);
            CHECK(d.subjects[0]->update.load() == updates);
            world->Stop();
            return d;
        }});

        rows.push_back({"caller-driven world destruction", false, [](EventLog &log) {
            ecs::Manager manager;
            auto world = std::make_unique<ecs::Container>(&manager, "departure");
            Departure d;
            d.reverse = true;
            d.subjects = addSystems(world.get(), log, 4);
            passes(world.get(), 2);

            world.reset();
            for(const auto &c : d.subjects)
            {
                CHECK(c->shutdown.load() == 1);
                CHECK(c->shutdownThread() == std::this_thread::get_id());
            }
            return d;
        }});

        // No assertions run inside this one: it runs on a watchdog worker thread.
        rows.push_back({"destruction of a threaded world", true, [](EventLog &log) {
            ecs::Manager manager;
            auto world = std::make_unique<ecs::Container>(&manager, "departure");
            Departure d;
            d.reverse = true;
            d.onWorldThread = true;
            d.subjects = addSystems(world.get(), log, 4);
            world->Start(200);
            const auto limit = std::chrono::seconds(20 * kSanitizerFactor);
            for(const auto &c : d.subjects)
            {
                waitUntil([&] { return c->update.load() >= 1; }, limit);
            }
            world.reset();
            return d;
        }});

        return rows;
    }
}

TEST_CASE("Departure table: shutdown exactly once, in reverse order", "[Lifecycle]")
{
    const auto table = departureRows();
    const auto row = GENERATE_COPY(from_range(table));

    DYNAMIC_SECTION(row.name)
    {
        EventLog log;
        Departure d;
        if(row.threaded)
        {
            d = withTimeout([&] { return row.run(log); }, std::chrono::seconds(60 * kSanitizerFactor));
        }
        else
        {
            d = row.run(log);
        }

        REQUIRE(!d.subjects.empty());
        for(const auto &c : d.subjects)
        {
            CHECK(c->initialize.load() == 1);
            REQUIRE(c->shutdown.load() == 1);
            // The notification follows the start-up and, for a system that ran, its updates.
            CHECK(c->shutdownSequence.load() > c->startSequence.load());
        }
        for(const auto &c : d.neverStarted)
        {
            CHECK(c->initialize.load() == 0);
            CHECK(c->shutdown.load() == 0);
        }
        if(d.reverse)
        {
            for(size_t i = 1; i < d.subjects.size(); i++)
            {
                CHECK(d.subjects[i]->shutdownSequence.load() < d.subjects[i - 1]->shutdownSequence.load());
            }
        }
        if(d.onWorldThread)
        {
            const auto worldThread = d.subjects[0]->updateThread();
            CHECK(worldThread != std::this_thread::get_id());
            for(const auto &c : d.subjects)
            {
                CHECK(c->shutdownThread() == worldThread);
            }
        }
        else
        {
            for(const auto &c : d.subjects)
            {
                CHECK(c->shutdownThread() == std::this_thread::get_id());
            }
        }
    }
}

TEST_CASE("Five systems, middle one throws in Shutdown", "[Lifecycle]")
{
    EventLog log;
    auto sink = std::make_shared<LogSink>();
    ecs::Manager manager;
    auto world = std::make_unique<ecs::Container>(&manager, "throwing");
    sinkInstall(world.get(), sink);
    std::vector<std::shared_ptr<Counters>> counters;
    for(int i = 1; i <= 5; i++)
    {
        auto system = std::make_unique<CountingSystem>(&log, "s" + std::to_string(i));
        if(i == 3) system->throwInShutdown = true;
        counters.push_back(system->counters);
        world->System(std::move(system));
    }
    passes(world.get(), 2);

    SECTION("the log destination accepts every line")
    {
        REQUIRE_NOTHROW(world.reset());
    }

    SECTION("the log destination throws when it is given an error")
    {
        sink->throwOnError = true;
        REQUIRE_NOTHROW(world.reset());
    }

    for(const auto &c : counters)
    {
        CHECK(c->shutdown.load() == 1);
    }
    const std::vector<std::string> expected = {"s5", "s4", "s3", "s2", "s1"};
    CHECK(log.handlesOf("shutdown") == expected);

    // The line reaches the destination, with the identifier of the system that threw.
    const long index = sink->indexOf("s3", "error");
    CHECK(index >= 0);
    CHECK(sink->indexOf("s1", "error") == -1);
    CHECK(sink->indexOf("s4", "error") == -1);
}

TEST_CASE("Removal path swallows a throwing Shutdown", "[Lifecycle]")
{
    EventLog log;
    auto sink = std::make_shared<LogSink>();
    ecs::Manager manager;
    auto *world = manager.Container("removal");
    sinkInstall(world, sink);
    auto bad = std::make_unique<CountingSystem>(&log, "bad");
    bad->throwInShutdown = true;
    auto badCounters = bad->counters;
    world->System(std::move(bad));
    auto good = std::make_unique<CountingSystem>(&log, "good");
    auto goodCounters = good->counters;
    world->System(std::move(good));
    passes(world, 2);

    REQUIRE_NOTHROW(world->SystemDestroy("bad"));
    CHECK(badCounters->shutdown.load() == 1);
    CHECK(sink->indexOf("bad", "error") >= 0);
    CHECK_FALSE(world->Systems.contains("bad"));

    // The later releases still proceed.
    REQUIRE_NOTHROW(world->SystemDestroy("good"));
    CHECK(goodCounters->shutdown.load() == 1);
    CHECK_FALSE(world->Systems.contains("good"));
    REQUIRE_NOTHROW(world->Update());

    SECTION("a removal inside a pass is also swallowed")
    {
        auto bad2 = std::make_unique<CountingSystem>(&log, "bad2");
        bad2->throwInShutdown = true;
        auto bad2Counters = bad2->counters;
        world->System(std::move(bad2));
        auto remover = std::make_unique<CountingSystem>(&log, "remover");
        auto done = std::make_shared<bool>(false);
        remover->onUpdate = [world, done] {
            if(*done) return;
            *done = true;
            world->SystemDestroy("bad2");
        };
        world->System(std::move(remover));
        world->Update();
        REQUIRE_NOTHROW(world->Update());
        CHECK(bad2Counters->shutdown.load() == 1);
        CHECK(sink->indexOf("bad2", "error") >= 0);
    }
}

TEST_CASE("Never-started system gets no notification", "[Lifecycle]")
{
    EventLog log;
    ecs::Manager manager;

    SECTION("removed before any pass")
    {
        auto *world = manager.Container("never");
        auto system = std::make_unique<CountingSystem>(&log, "idle");
        auto counters = system->counters;
        world->System(std::move(system));
        world->SystemDestroy("idle");
        passes(world, 3);
        CHECK(counters->initialize.load() == 0);
        CHECK(counters->shutdown.load() == 0);
    }

    SECTION("world destroyed before any pass")
    {
        auto world = std::make_unique<ecs::Container>(&manager, "never");
        auto system = std::make_unique<CountingSystem>(&log, "idle");
        auto counters = system->counters;
        world->System(std::move(system));
        world.reset();
        CHECK(counters->initialize.load() == 0);
        CHECK(counters->shutdown.load() == 0);
    }

    SECTION("replaced before any pass")
    {
        auto world = std::make_unique<ecs::Container>(&manager, "never");
        auto first = std::make_unique<CountingSystem>(&log, "idle");
        auto firstCounters = first->counters;
        world->System(std::move(first));
        auto second = std::make_unique<CountingSystem>(&log, "idle");
        auto secondCounters = second->counters;
        world->System(std::move(second));
        CHECK(firstCounters->shutdown.load() == 0);
        world.reset();
        CHECK(firstCounters->initialize.load() == 0);
        CHECK(secondCounters->initialize.load() == 0);
        CHECK(secondCounters->shutdown.load() == 0);
    }

    SECTION("a system whose start-up failed is notified once when it leaves")
    {
        auto world = std::make_unique<ecs::Container>(&manager, "never");
        auto system = std::make_unique<CountingSystem>(&log, "broken");
        system->throwInInitialize = true;
        auto counters = system->counters;
        world->System(std::move(system));
        REQUIRE_THROWS_AS(world->Update(), std::runtime_error);
        CHECK(counters->initialize.load() == 1);
        CHECK(counters->shutdown.load() == 0);
        world->SystemDestroy("broken");
        CHECK(counters->shutdown.load() == 1);
        world.reset();
        CHECK(counters->shutdown.load() == 1);
    }

    SECTION("a system whose start-up failed is notified once when the world goes")
    {
        auto world = std::make_unique<ecs::Container>(&manager, "never");
        auto system = std::make_unique<CountingSystem>(&log, "broken");
        system->throwInInitialize = true;
        auto counters = system->counters;
        world->System(std::move(system));
        REQUIRE_THROWS_AS(world->Update(), std::runtime_error);
        world.reset();
        CHECK(counters->shutdown.load() == 1);
    }
}

TEST_CASE("Changes during teardown", "[Lifecycle]")
{
    struct Result
    {
        std::vector<std::shared_ptr<Counters>> counters;
        std::shared_ptr<Counters> extra;
        std::vector<std::string> shutdownOrder;
    };

    // The last registered system is notified first. From its notification it removes s2, registers a
    // new system and asks the world to stop.
    auto scenario = [](bool useStop) {
        return withTimeout([useStop] {
            EventLog log;
            ecs::Manager manager;
            auto world = std::make_unique<ecs::Container>(&manager, "teardown");
            Result result;
            result.counters = addSystems(world.get(), log, 3);
            auto last = std::make_unique<CountingSystem>(&log, "s4");
            result.counters.push_back(last->counters);
            auto extra = std::make_shared<std::shared_ptr<Counters>>();
            auto *raw = world.get();
            last->onShutdown = [raw, &log, extra] {
                raw->SystemDestroy("s2");
                auto added = std::make_unique<CountingSystem>(&log, "extra");
                *extra = added->counters;
                raw->System(std::move(added));
                raw->Stop();
            };
            world->System(std::move(last));
            passes(world.get(), 2);
            if(useStop)
            {
                world->Stop();
            }
            world.reset();
            result.extra = *extra;
            result.shutdownOrder = log.handlesOf("shutdown");
            return result;
        }, std::chrono::seconds(60 * kSanitizerFactor));
    };

    SECTION("during a stop")
    {
        const Result r = scenario(true);
        for(const auto &c : r.counters) CHECK(c->shutdown.load() == 1);
        REQUIRE(r.extra);
        CHECK(r.extra->initialize.load() == 0);
        CHECK(r.extra->shutdown.load() == 0);
        CHECK(r.shutdownOrder.size() == 4);
        CHECK(r.shutdownOrder.front() == "s4");
    }

    SECTION("during destruction")
    {
        const Result r = scenario(false);
        for(const auto &c : r.counters) CHECK(c->shutdown.load() == 1);
        REQUIRE(r.extra);
        CHECK(r.extra->initialize.load() == 0);
        CHECK(r.extra->shutdown.load() == 0);
        CHECK(r.shutdownOrder.size() == 4);
        CHECK(r.shutdownOrder.front() == "s4");
    }
}

TEST_CASE("Messages to a shut-down system are refused", "[Lifecycle]")
{
    EventLog log;
    ecs::Manager manager;
    auto *world = manager.Container("refusing");

    SECTION("after the system was removed")
    {
        auto system = std::make_unique<CountingSystem>(&log, "gone");
        auto counters = system->counters;
        world->System(std::move(system));
        passes(world, 2);
        world->SystemDestroy("gone");
        CHECK(counters->shutdown.load() == 1);
        CHECK_THROWS_AS(world->MessageSubmit(messageTo("gone")), std::runtime_error);
        nlohmann::json viaManager = messageTo("gone");
        viaManager["destination"]["container"] = "refusing";
        CHECK_THROWS_AS(manager.MessageSubmit(viaManager), std::runtime_error);
        passes(world, 2);
        CHECK(counters->update.load() == 2);
    }

    SECTION("by a notification that comes later than the receiver's")
    {
        // s1 is notified last, after s3 has been shut down and its mailbox entry removed.
        auto systems = addSystems(world, log, 3);
        auto *first = static_cast<CountingSystem *>(world->Systems.at("s1").get());
        auto outcome = std::make_shared<int>(-1);
        first->onShutdown = [world, outcome] {
            try
            {
                world->MessageSubmit(messageTo("s3"));
                *outcome = 1;
            }
            catch(const std::runtime_error &)
            {
                *outcome = 0;
            }
        };
        passes(world, 2);
        const int updates = systems[2]->update.load();
        world->Stop();
        // Refused or dropped; it is never delivered to a released object, and nothing is updated again.
        CHECK(*outcome >= 0);
        CHECK(systems[2]->shutdown.load() == 1);
        CHECK(systems[2]->update.load() == updates);
    }

    SECTION("a torn-down world does not start or update anything")
    {
        auto systems = addSystems(world, log, 2);
        passes(world, 2);
        world->Stop();
        auto late = std::make_unique<CountingSystem>(&log, "late");
        auto lateCounters = late->counters;
        world->System(std::move(late));
        const int updates = systems[0]->update.load();
        REQUIRE_NOTHROW(passes(world, 5));
        CHECK(lateCounters->initialize.load() == 0);
        CHECK(lateCounters->update.load() == 0);
        CHECK(systems[0]->update.load() == updates);
        CHECK(systems[0]->shutdown.load() == 1);
    }
}

TEST_CASE("Replacement shuts down the old instance", "[Lifecycle]")
{
    EventLog log;
    ecs::Manager manager;
    auto *world = manager.Container("replacing");
    auto original = std::make_unique<CountingSystem>(&log, "subject");
    auto originalCounters = original->counters;
    world->System(std::move(original));
    passes(world, 2);

    SECTION("replaced outside a pass")
    {
        auto replacement = std::make_unique<CountingSystem>(&log, "subject");
        auto replacementCounters = replacement->counters;
        world->System(std::move(replacement));
        CHECK(originalCounters->shutdown.load() == 1);
        CHECK(replacementCounters->initialize.load() == 0);

        world->Update();
        CHECK(replacementCounters->initialize.load() == 1);
        CHECK(originalCounters->shutdownSequence.load() < replacementCounters->startSequence.load());
        CHECK(replacementCounters->startSequence.load() < replacementCounters->firstUpdateSequence.load());
        passes(world, 10);
        CHECK(originalCounters->shutdown.load() == 1);
        CHECK(originalCounters->update.load() == 2);
        CHECK(replacementCounters->initialize.load() == 1);
        CHECK(replacementCounters->shutdown.load() == 0);
    }

    SECTION("replaced from inside a pass")
    {
        Subject replacement = makeSubject(log, "subject");
        auto registrar = std::make_unique<CountingSystem>(&log, "registrar");
        auto done = std::make_shared<bool>(false);
        auto atCall = std::make_shared<int>(-1);
        registrar->onUpdate = [world, replacement, done, atCall, originalCounters] {
            if(*done) return;
            *done = true;
            registerSubject(world, replacement);
            *atCall = originalCounters->shutdown.load();
        };
        world->System(std::move(registrar));

        world->Update();
        // Not nested in the call, but delivered by the end of the pass.
        CHECK(*atCall == 0);
        CHECK(originalCounters->shutdown.load() == 1);
        passes(world, 3);
        REQUIRE(replacement.counters->initialize.load() == 1);
        CHECK(originalCounters->shutdownSequence.load() < replacement.counters->startSequence.load());
        CHECK(originalCounters->shutdown.load() == 1);
        // It was still updated in the pass that replaced it, because it comes before the registrar.
        CHECK(originalCounters->update.load() == 3);
    }
}

namespace
{
    // The ways a system can leave a world while it logs from Shutdown() and from its destructor.
    std::unique_ptr<CountingSystem> loggingSystem(EventLog &log, const std::string &handle)
    {
        auto system = std::make_unique<CountingSystem>(&log, handle);
        system->logInHooks = true;
        return system;
    }

    struct LoggingRow
    {
        std::string name;
        bool threaded;
        std::function<void(EventLog &, const std::shared_ptr<LogSink> &)> run;
    };

    std::vector<LoggingRow> loggingRows()
    {
        std::vector<LoggingRow> rows;

        rows.push_back({"removal outside a pass", false, [](EventLog &log, const std::shared_ptr<LogSink> &sink) {
            ecs::Manager manager;
            auto *world = manager.Container("logging");
            sinkInstall(world, sink);
            world->System(loggingSystem(log, "a"));
            world->System(loggingSystem(log, "b"));
            passes(world, 2);
            world->SystemDestroy("a");
            world->SystemDestroy("b");
        }});

        rows.push_back({"removal inside a pass", false, [](EventLog &log, const std::shared_ptr<LogSink> &sink) {
            ecs::Manager manager;
            auto *world = manager.Container("logging");
            sinkInstall(world, sink);
            world->System(loggingSystem(log, "a"));
            world->System(loggingSystem(log, "b"));
            auto remover = std::make_unique<CountingSystem>(&log, "remover");
            auto done = std::make_shared<bool>(false);
            remover->onUpdate = [world, done] {
                if(*done) return;
                *done = true;
                world->SystemDestroy("a");
                world->SystemDestroy("b");
            };
            world->System(std::move(remover));
            passes(world, 3);
        }});

        rows.push_back({"caller-driven Stop()", false, [](EventLog &log, const std::shared_ptr<LogSink> &sink) {
            ecs::Manager manager;
            auto *world = manager.Container("logging");
            sinkInstall(world, sink);
            world->System(loggingSystem(log, "a"));
            world->System(loggingSystem(log, "b"));
            passes(world, 2);
            world->Stop();
        }});

        rows.push_back({"caller-driven world destruction", false, [](EventLog &log, const std::shared_ptr<LogSink> &sink) {
            ecs::Manager manager;
            auto world = std::make_unique<ecs::Container>(&manager, "logging");
            sinkInstall(world.get(), sink);
            world->System(loggingSystem(log, "a"));
            world->System(loggingSystem(log, "b"));
            passes(world.get(), 2);
            world.reset();
        }});

        rows.push_back({"replacement", false, [](EventLog &log, const std::shared_ptr<LogSink> &sink) {
            ecs::Manager manager;
            auto *world = manager.Container("logging");
            sinkInstall(world, sink);
            world->System(loggingSystem(log, "a"));
            world->System(loggingSystem(log, "b"));
            passes(world, 2);
            world->System(std::make_unique<CountingSystem>(&log, "a"));
            world->System(std::make_unique<CountingSystem>(&log, "b"));
            passes(world, 2);
        }});

        // No assertions run inside this one: it runs on a watchdog worker thread.
        rows.push_back({"destruction of a threaded world", true, [](EventLog &log, const std::shared_ptr<LogSink> &sink) {
            ecs::Manager manager;
            auto world = std::make_unique<ecs::Container>(&manager, "logging");
            sinkInstall(world.get(), sink);
            auto a = loggingSystem(log, "a");
            auto b = loggingSystem(log, "b");
            auto aCounters = a->counters;
            auto bCounters = b->counters;
            world->System(std::move(a));
            world->System(std::move(b));
            world->Start(200);
            const auto limit = std::chrono::seconds(20 * kSanitizerFactor);
            waitUntil([&] { return aCounters->update.load() >= 1 && bCounters->update.load() >= 1; }, limit);
            world.reset();
        }});

        return rows;
    }
}

TEST_CASE("Systems may log while being torn down", "[Lifecycle]")
{
    const auto table = loggingRows();
    const auto row = GENERATE_COPY(from_range(table));

    DYNAMIC_SECTION(row.name)
    {
        EventLog log;
        auto sink = std::make_shared<LogSink>();
        if(row.threaded)
        {
            withTimeout([&] { row.run(log, sink); return 0; }, std::chrono::seconds(60 * kSanitizerFactor));
        }
        else
        {
            row.run(log, sink);
        }

        for(const std::string handle : {"a", "b"})
        {
            const long down = sink->indexOf("shutting down " + handle);
            const long gone = sink->indexOf("destroying " + handle);
            CHECK(sink->count("shutting down " + handle) == 1);
            CHECK(sink->count("destroying " + handle) == 1);
            CHECK(down >= 0);
            CHECK(gone >= 0);
            // The destructor logs after the notification did.
            CHECK(down < gone);
        }
    }
}
