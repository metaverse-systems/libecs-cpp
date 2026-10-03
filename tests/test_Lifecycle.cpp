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
            this->Timing.SetInterval(std::chrono::microseconds(0));
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
        rows.push_back({"Stop() of a threaded world", true, [](EventLog &log) {
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
            world->Stop();
            // The world has ended when Stop() returns: nothing is updated or notified afterwards.
            const int updates = d.subjects[0]->update.load();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if(d.subjects[0]->update.load() != updates) d.subjects.clear();
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

        // No assertions run inside this one: it runs on a watchdog worker thread.
        rows.push_back({"Manager::Shutdown()", true, [](EventLog &log) {
            ecs::Manager manager;
            Departure d;
            d.reverse = true;
            d.onWorldThread = true;
            auto *world = manager.Container("departure");
            d.subjects = addSystems(world, log, 4);
            world->Start(200);
            const auto limit = std::chrono::seconds(20 * kSanitizerFactor);
            for(const auto &c : d.subjects)
            {
                waitUntil([&] { return c->update.load() >= 1; }, limit);
            }
            manager.Shutdown();
            // The world has ended when Shutdown() returns: nothing is updated or notified afterwards.
            const int updates = d.subjects[0]->update.load();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if(d.subjects[0]->update.load() != updates) d.subjects.clear();
            return d;
        }});

        // No assertions run inside this one: it runs on a watchdog worker thread.
        rows.push_back({"manager destruction", true, [](EventLog &log) {
            Departure d;
            d.reverse = true;
            d.onWorldThread = true;
            {
                ecs::Manager manager;
                auto *world = manager.Container("departure");
                d.subjects = addSystems(world, log, 4);
                world->Start(200);
                const auto limit = std::chrono::seconds(20 * kSanitizerFactor);
                for(const auto &c : d.subjects)
                {
                    waitUntil([&] { return c->update.load() >= 1; }, limit);
                }
            }
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

TEST_CASE("A world driven by Update() stopped from inside its own pass", "[Lifecycle]")
{
    EventLog log;
    ecs::Manager manager;
    ecs::Container world(&manager, "inner-stop");

    auto first = std::make_unique<CountingSystem>(&log, "first");
    auto stopper = std::make_unique<CountingSystem>(&log, "stopper");
    auto last = std::make_unique<CountingSystem>(&log, "last");
    auto firstCounters = first->counters;
    auto stopperCounters = stopper->counters;
    auto lastCounters = last->counters;
    ecs::Container *raw = &world;
    stopper->onUpdate = [raw] { raw->Stop(); };
    world.System(std::move(first));
    world.System(std::move(stopper));
    world.System(std::move(last));

    // The world is torn down at once, on the calling thread. The rest of the pass does not update a
    // system that has already been shut down.
    world.Update();
    CHECK(firstCounters->shutdown.load() == 1);
    CHECK(stopperCounters->shutdown.load() == 1);
    CHECK(lastCounters->shutdown.load() == 1);
    CHECK(lastCounters->update.load() == 0);
    CHECK(firstCounters->update.load() == 1);

    world.Update();
    CHECK(firstCounters->update.load() == 1);
    CHECK(lastCounters->shutdown.load() == 1);
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

namespace
{
    using Clock = std::chrono::steady_clock;

    std::chrono::seconds watchdogLimit()
    {
        return std::chrono::seconds(60 * kSanitizerFactor);
    }

    long millisecondsSince(Clock::time_point start)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    }

    // Waits until the system has been updated at least once.
    bool waitForUpdate(const std::shared_ptr<Counters> &c)
    {
        return waitUntil([&] { return c->update.load() >= 1; }, std::chrono::seconds(20 * kSanitizerFactor));
    }

    // What a test saw of a world that was started many times.
    struct RepeatedStart
    {
        int starts = 0;
        std::thread::id threadBefore;
        std::thread::id threadAfter;
        int passesInWindow = 0;
        bool threwOnStart = false;
    };

    RepeatedStart startManyTimes(bool differentIntervals)
    {
        RepeatedStart out;
        EventLog log;
        ecs::Manager manager;
        auto world = std::make_unique<ecs::Container>(&manager, "repeated");
        auto system = std::make_unique<CountingSystem>(&log, "subject");
        auto counters = system->counters;
        world->System(std::move(system));
        // Every 20 ms.
        world->Start(20000);
        waitForUpdate(counters);
        out.threadBefore = counters->updateThread();
        try
        {
            for(int i = 0; i < 99; i++)
            {
                if(differentIntervals) world->Start(1000 + i);
                else world->Start(20000);
            }
        }
        catch(...)
        {
            out.threwOnStart = true;
        }
        const int before = counters->update.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        out.passesInWindow = counters->update.load() - before;
        out.threadAfter = counters->updateThread();
        out.starts = counters->initialize.load();
        return out;
    }
}

TEST_CASE("Start called 100 times", "[Lifecycle]")
{
    const bool differentIntervals = GENERATE(false, true);
    DYNAMIC_SECTION(std::string(differentIntervals ? "with different intervals" : "with the same interval"))
    {
        const RepeatedStart r = withTimeout([&] { return startManyTimes(differentIntervals); }, watchdogLimit());
        CHECK_FALSE(r.threwOnStart);
        CHECK(r.starts == 1);
        CHECK(r.threadBefore != std::thread::id());
        CHECK(r.threadAfter == r.threadBefore);
        // 500 ms at 20 ms is 25 passes. A changed interval would give several hundred.
        CHECK(r.passesInWindow >= 5);
        CHECK(r.passesInWindow <= 40);
    }
}

namespace
{
    struct AfterStop
    {
        int startsBefore = 0;
        int startsAfter = 0;
        int updatesBefore = 0;
        int updatesAfter = 0;
        int shutdownsAfter = 0;
        std::thread::id threadBefore;
        std::thread::id threadAfter;
        bool threw = false;
    };

    AfterStop startAfter(bool managerShutdown)
    {
        AfterStop out;
        EventLog log;
        ecs::Manager manager;
        auto world = std::make_unique<ecs::Container>(&manager, "after");
        auto system = std::make_unique<CountingSystem>(&log, "subject");
        auto counters = system->counters;
        world->System(std::move(system));
        if(managerShutdown)
        {
            manager.Shutdown();
        }
        else
        {
            world->Start(1000);
            waitForUpdate(counters);
            world->Stop();
        }
        out.startsBefore = counters->initialize.load();
        out.updatesBefore = counters->update.load();
        out.threadBefore = counters->updateThread();
        try
        {
            world->Start();
            world->Start(500);
        }
        catch(...)
        {
            out.threw = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        out.startsAfter = counters->initialize.load();
        out.updatesAfter = counters->update.load();
        out.shutdownsAfter = counters->shutdown.load();
        out.threadAfter = counters->updateThread();
        return out;
    }
}

TEST_CASE("Start after Stop does nothing", "[Lifecycle]")
{
    const AfterStop r = withTimeout([] { return startAfter(false); }, watchdogLimit());
    CHECK_FALSE(r.threw);
    CHECK(r.startsBefore == 1);
    CHECK(r.startsAfter == 1);
    CHECK(r.updatesAfter == r.updatesBefore);
    CHECK(r.shutdownsAfter == 1);
    CHECK(r.threadAfter == r.threadBefore);
}

TEST_CASE("Start after manager shutdown does nothing", "[Lifecycle]")
{
    const AfterStop r = withTimeout([] { return startAfter(true); }, watchdogLimit());
    CHECK_FALSE(r.threw);
    // No thread was created: the system was never started or updated.
    CHECK(r.startsAfter == 0);
    CHECK(r.updatesAfter == 0);
    CHECK(r.updatesBefore == 0);
}

namespace
{
    // The system's start-up waits on a gate that the test opens.
    struct Gate
    {
        std::atomic<bool> open{false};
    };

    struct GatedStart
    {
        bool startReturnedWhileBlocked = false;
        int startsAfterStart = -1;
        int startsAfterRelease = 0;
        std::thread::id initializeThread;
        std::thread::id updateThread;
    };

    GatedStart startGated()
    {
        GatedStart out;
        EventLog log;
        ecs::Manager manager;
        auto world = std::make_unique<ecs::Container>(&manager, "gated");
        auto gate = std::make_shared<Gate>();
        auto system = std::make_unique<CountingSystem>(&log, "subject");
        auto counters = system->counters;
        system->onInitialize = [gate] {
            waitUntil([&] { return gate->open.load(); }, std::chrono::seconds(20 * kSanitizerFactor));
        };
        world->System(std::move(system));

        // If Start() ran the start-up on the calling thread it would block on the gate here.
        std::promise<void> returned;
        auto returnedFuture = returned.get_future();
        std::thread caller([&] { world->Start(1000); returned.set_value(); });
        out.startReturnedWhileBlocked = returnedFuture.wait_for(std::chrono::seconds(5 * kSanitizerFactor)) == std::future_status::ready;
        gate->open.store(true);
        caller.join();
        out.startsAfterStart = counters->update.load();
        waitForUpdate(counters);
        out.startsAfterRelease = counters->initialize.load();
        out.initializeThread = counters->initializeThread();
        out.updateThread = counters->updateThread();
        return out;
    }
}

TEST_CASE("Start on a caller-driven world", "[Lifecycle]")
{
    SECTION("Start only starts the thread")
    {
        const GatedStart r = withTimeout([] { return startGated(); }, watchdogLimit());
        CHECK(r.startReturnedWhileBlocked);
        CHECK(r.startsAfterStart == 0);
        CHECK(r.startsAfterRelease == 1);
        CHECK(r.initializeThread != std::this_thread::get_id());
        CHECK(r.initializeThread == r.updateThread);
    }

    SECTION("without Start the first Update() starts the systems")
    {
        EventLog log;
        ecs::Manager manager;
        auto *world = manager.Container("driven");
        Subject subject = makeSubject(log, "subject");
        registerSubject(world, subject);
        CHECK(subject.counters->initialize.load() == 0);
        world->Update();
        CHECK(subject.counters->initialize.load() == 1);
        CHECK(subject.counters->initializeThread() == std::this_thread::get_id());
        CHECK(subject.counters->startSequence.load() < subject.counters->firstUpdateSequence.load());
    }
}

namespace
{
    // Time Stop() or destruction of an idle threaded world.
    long idleStopTime(uint32_t interval, bool byDestruction)
    {
        EventLog log;
        ecs::Manager manager;
        auto world = std::make_unique<ecs::Container>(&manager, "idle");
        world->System(std::make_unique<CountingSystem>(&log, "subject"));
        world->Start(interval);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const auto begin = Clock::now();
        if(byDestruction) world.reset();
        else world->Stop();
        return millisecondsSince(begin);
    }

    struct StopTimeRow
    {
        std::string name;
        uint32_t interval;
        bool byDestruction;
    };
}

TEST_CASE("Stop time is independent of the interval", "[Lifecycle]")
{
    const std::vector<StopTimeRow> table{
        {"Stop() with a 5 s interval", 5000000, false},
        {"destruction with a 5 s interval", 5000000, true},
        {"Stop() with a 1 ms interval", 1000, false},
        {"destruction with a 1 ms interval", 1000, true},
    };
    const auto row = GENERATE_COPY(from_range(table));
    DYNAMIC_SECTION(row.name)
    {
        const long elapsed = withTimeout([&] { return idleStopTime(row.interval, row.byDestruction); }, watchdogLimit());
        CHECK(elapsed < 250 * kSanitizerFactor);
    }
}

namespace
{
    struct FrozenWorld
    {
        int updatesAtReturn = 0;
        int updatesLater = 0;
        int shutdowns = 0;
        int starts = 0;
    };

    FrozenWorld stopAndWatch()
    {
        FrozenWorld out;
        EventLog log;
        ecs::Manager manager;
        auto world = std::make_unique<ecs::Container>(&manager, "frozen");
        auto system = std::make_unique<CountingSystem>(&log, "subject");
        auto counters = system->counters;
        world->System(std::move(system));
        world->Start(1000);
        waitUntil([&] { return counters->update.load() >= 5; }, std::chrono::seconds(20 * kSanitizerFactor));
        world->Stop();
        out.updatesAtReturn = counters->update.load();
        // The count never changes during the 200 ms that follow.
        bool changed = false;
        for(int i = 0; i < 20; i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if(counters->update.load() != out.updatesAtReturn) changed = true;
        }
        out.updatesLater = changed ? -1 : out.updatesAtReturn;
        out.shutdowns = counters->shutdown.load();
        out.starts = counters->initialize.load();
        return out;
    }
}

TEST_CASE("No update after the stop request is observed", "[Lifecycle]")
{
    const FrozenWorld r = withTimeout([] { return stopAndWatch(); }, watchdogLimit());
    CHECK(r.updatesAtReturn >= 5);
    CHECK(r.updatesLater == r.updatesAtReturn);
    CHECK(r.starts == 1);
    CHECK(r.shutdowns == 1);
}

namespace
{
    struct SlowShutdown
    {
        bool finishedAtReturn = false;
        long elapsed = 0;
        int notifications = 0;
    };

    SlowShutdown stopSlowShutdown()
    {
        SlowShutdown out;
        EventLog log;
        ecs::Manager manager;
        auto world = std::make_unique<ecs::Container>(&manager, "slow");
        auto system = std::make_unique<CountingSystem>(&log, "subject");
        auto counters = system->counters;
        auto finished = std::make_shared<std::atomic<bool>>(false);
        system->onShutdown = [finished] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            finished->store(true);
        };
        world->System(std::move(system));
        world->Start(1000);
        waitForUpdate(counters);
        const auto begin = Clock::now();
        world->Stop();
        out.elapsed = millisecondsSince(begin);
        out.finishedAtReturn = finished->load();
        out.notifications = counters->shutdown.load();
        return out;
    }
}

TEST_CASE("Stop waits for the notification", "[Lifecycle]")
{
    const SlowShutdown r = withTimeout([] { return stopSlowShutdown(); }, watchdogLimit());
    CHECK(r.finishedAtReturn);
    CHECK(r.notifications == 1);
    CHECK(r.elapsed >= 90);
}

namespace
{
    struct ConcurrentStops
    {
        std::vector<int> finishedAtReturn;
        int notifications = 0;
    };

    ConcurrentStops stopFromManyThreads()
    {
        ConcurrentStops out;
        EventLog log;
        ecs::Manager manager;
        auto world = std::make_unique<ecs::Container>(&manager, "concurrent");
        auto system = std::make_unique<CountingSystem>(&log, "subject");
        auto counters = system->counters;
        auto finished = std::make_shared<std::atomic<bool>>(false);
        system->onShutdown = [finished] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            finished->store(true);
        };
        world->System(std::move(system));
        world->Start(1000);
        waitForUpdate(counters);

        constexpr int callers = 4;
        std::atomic<bool> go{false};
        std::vector<int> seen(callers, 0);
        std::vector<std::thread> threads;
        for(int i = 0; i < callers; i++)
        {
            threads.emplace_back([&, i] {
                while(!go.load()) std::this_thread::yield();
                world->Stop();
                seen[i] = finished->load() ? 1 : 0;
            });
        }
        go.store(true);
        for(auto &t : threads) t.join();
        out.finishedAtReturn = seen;
        out.notifications = counters->shutdown.load();
        return out;
    }
}

TEST_CASE("Concurrent Stop callers", "[Lifecycle]")
{
    const ConcurrentStops r = withTimeout([] { return stopFromManyThreads(); }, watchdogLimit());
    REQUIRE(r.finishedAtReturn.size() == 4);
    for(int seen : r.finishedAtReturn) CHECK(seen == 1);
    CHECK(r.notifications == 1);
}

namespace
{
    struct SelfStops
    {
        int repetitions = 0;
        int returnedAtOnce = 0;
        int notNested = 0;
        int updatedOnce = 0;
        int notifiedOnce = 0;
        int laterStopAtOnce = 0;
    };

    SelfStops stopFromOwnUpdate(int repetitions)
    {
        SelfStops out;
        EventLog log;
        ecs::Manager manager;
        for(int rep = 0; rep < repetitions; rep++)
        {
            auto world = std::make_unique<ecs::Container>(&manager, "self");
            auto system = std::make_unique<CountingSystem>(&log, "subject");
            auto counters = system->counters;
            auto callMs = std::make_shared<std::atomic<long>>(-1);
            auto shutdownAtReturn = std::make_shared<std::atomic<int>>(-1);
            auto done = std::make_shared<std::atomic<bool>>(false);
            ecs::Container *raw = world.get();
            system->onUpdate = [raw, counters, callMs, shutdownAtReturn, done] {
                if(done->exchange(true)) return;
                const auto begin = Clock::now();
                raw->Stop();
                callMs->store(millisecondsSince(begin));
                shutdownAtReturn->store(counters->shutdown.load());
            };
            world->System(std::move(system));
            world->Start(500);
            waitUntil([&] { return callMs->load() >= 0; }, std::chrono::seconds(20 * kSanitizerFactor));
            const auto begin = Clock::now();
            world->Stop();
            const long later = millisecondsSince(begin);

            out.repetitions++;
            if(callMs->load() >= 0 && callMs->load() < 250 * kSanitizerFactor) out.returnedAtOnce++;
            if(shutdownAtReturn->load() == 0) out.notNested++;
            if(counters->update.load() == 1) out.updatedOnce++;
            if(counters->shutdown.load() == 1) out.notifiedOnce++;
            if(later < 250 * kSanitizerFactor) out.laterStopAtOnce++;
        }
        return out;
    }
}

TEST_CASE("Stop from a world thread only requests", "[Lifecycle]")
{
    const int repetitions = 500 / kSanitizerFactor;
    const SelfStops r = withTimeout([&] { return stopFromOwnUpdate(repetitions); }, watchdogLimit());
    CHECK(r.repetitions == repetitions);
    CHECK(r.returnedAtOnce == repetitions);
    CHECK(r.notNested == repetitions);
    CHECK(r.updatedOnce == repetitions);
    CHECK(r.notifiedOnce == repetitions);
    CHECK(r.laterStopAtOnce == repetitions);
}

namespace
{
    struct CrossStop
    {
        bool returnedBeforeEnd = false;
        long callMs = -1;
        bool finishedAfterTestStop = false;
        int notifications = 0;
    };

    CrossStop stopOtherWorld()
    {
        CrossStop out;
        EventLog log;
        ecs::Manager manager;
        auto a = std::make_unique<ecs::Container>(&manager, "a");
        auto b = std::make_unique<ecs::Container>(&manager, "b");

        auto bSystem = std::make_unique<CountingSystem>(&log, "b-subject");
        auto bCounters = bSystem->counters;
        auto finished = std::make_shared<std::atomic<bool>>(false);
        bSystem->onShutdown = [finished] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            finished->store(true);
        };
        b->System(std::move(bSystem));

        auto aSystem = std::make_unique<CountingSystem>(&log, "a-subject");
        auto aCounters = aSystem->counters;
        auto done = std::make_shared<std::atomic<bool>>(false);
        auto callMs = std::make_shared<std::atomic<long>>(-1);
        auto beforeEnd = std::make_shared<std::atomic<int>>(-1);
        ecs::Container *rawB = b.get();
        aSystem->onUpdate = [rawB, bCounters, finished, done, callMs, beforeEnd] {
            if(bCounters->update.load() < 1 || done->exchange(true)) return;
            const auto begin = Clock::now();
            rawB->Stop();
            callMs->store(millisecondsSince(begin));
            beforeEnd->store(finished->load() ? 0 : 1);
        };
        a->System(std::move(aSystem));

        b->Start(1000);
        a->Start(1000);
        waitUntil([&] { return callMs->load() >= 0; }, std::chrono::seconds(20 * kSanitizerFactor));
        out.callMs = callMs->load();
        out.returnedBeforeEnd = beforeEnd->load() == 1;
        b->Stop();
        out.finishedAfterTestStop = finished->load();
        out.notifications = bCounters->shutdown.load();
        a->Stop();
        return out;
    }
}

TEST_CASE("Stop of another world from a system returns at once", "[Lifecycle]")
{
    const CrossStop r = withTimeout([] { return stopOtherWorld(); }, watchdogLimit());
    CHECK(r.callMs >= 0);
    CHECK(r.callMs < 150 * kSanitizerFactor);
    CHECK(r.returnedBeforeEnd);
    CHECK(r.finishedAfterTestStop);
    CHECK(r.notifications == 1);
}

namespace
{
    struct Ticks
    {
        std::vector<Clock::time_point> times;
        int64_t spanMicroseconds = 0;
        int64_t expected = 0;
    };

    Ticks measureTicks()
    {
        Ticks out;
        constexpr int64_t interval = 20000;
        EventLog log;
        std::mutex lock;
        ecs::Manager manager;
        auto world = std::make_unique<ecs::Container>(&manager, "ticks");
        auto system = std::make_unique<CountingSystem>(&log, "subject");
        auto counters = system->counters;
        system->onUpdate = [&] {
            std::lock_guard<std::mutex> guard(lock);
            out.times.push_back(Clock::now());
        };
        world->System(std::move(system));
        world->Start(static_cast<uint32_t>(interval));
        waitForUpdate(counters);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        world->Stop();
        std::lock_guard<std::mutex> guard(lock);
        if(out.times.size() >= 2)
        {
            out.spanMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(out.times.back() - out.times.front()).count();
            out.expected = out.spanMicroseconds / interval;
        }
        return out;
    }
}

TEST_CASE("Tick period is unchanged", "[Lifecycle]")
{
    const Ticks r = withTimeout([] { return measureTicks(); }, watchdogLimit());
    REQUIRE(r.times.size() >= 2);
    // The same rule as test_Timing: the number of passes matches the whole intervals that elapsed, within one.
    const int64_t intervals = static_cast<int64_t>(r.times.size()) - 1;
    CHECK(intervals >= r.expected - 1);
    CHECK(intervals <= r.expected + 1);
    CHECK(r.expected >= 40);
}

namespace
{
    struct StartStopLoop
    {
        int iterations = 0;
        int consistent = 0;
    };

    StartStopLoop startStopLoop(int iterations)
    {
        StartStopLoop out;
        EventLog log;
        ecs::Manager manager;
        for(int i = 0; i < iterations; i++)
        {
            auto world = std::make_unique<ecs::Container>(&manager, "loop");
            auto system = std::make_unique<CountingSystem>(&log, "subject");
            auto counters = system->counters;
            world->System(std::move(system));
            world->Start(100);
            world->Stop();
            out.iterations++;
            // A system that was started was notified exactly once; one that was not, never.
            if(counters->shutdown.load() == counters->initialize.load() && counters->initialize.load() <= 1) out.consistent++;
        }
        return out;
    }
}

TEST_CASE("Stop and start loop", "[Lifecycle]")
{
    const int iterations = 500 / kSanitizerFactor;
    const StartStopLoop r = withTimeout([&] { return startStopLoop(iterations); }, watchdogLimit());
    CHECK(r.iterations == iterations);
    CHECK(r.consistent == iterations);
}

// Cases that cover what a manager does to its worlds when it shuts down and when it is destroyed.

namespace
{
#if defined(__SANITIZE_THREAD__)
    constexpr int kThreadFactor = 4;
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
    constexpr int kThreadFactor = 4;
#else
    constexpr int kThreadFactor = 1;
#endif
#else
    constexpr int kThreadFactor = 1;
#endif

    // Set when the thread that touched it ends. A world thread touches it from Update().
    struct EndMark
    {
        std::atomic<bool> *flag = nullptr;

        ~EndMark()
        {
            if(this->flag) this->flag->store(true);
        }
    };

    thread_local EndMark endMark;

    // Worlds of one manager that run on their own threads, with the systems they hold. Declare it before
    // the manager so the end flags outlive every world thread.
    struct Fleet
    {
        std::vector<ecs::Container *> worlds;
        // Per world, in registration order.
        std::vector<std::vector<std::shared_ptr<Counters>>> counters;
        std::vector<std::unique_ptr<std::atomic<bool>>> ended;
    };

    using Customize = std::function<void(ecs::Container *, int, int, CountingSystem &)>;

    // Builds threaded worlds named <prefix><n> holding systems named <prefix><n>-s<i>, starts them and
    // waits until every system has been updated.
    void fleetBuild(Fleet &fleet, ecs::Manager &manager, EventLog &log, int worlds, int perWorld, uint32_t interval,
                    const std::string &prefix, const Customize &customize = nullptr)
    {
        for(int w = 0; w < worlds; w++)
        {
            fleet.ended.push_back(std::make_unique<std::atomic<bool>>(false));
        }
        for(int w = 0; w < worlds; w++)
        {
            auto *world = manager.Container(prefix + std::to_string(w));
            fleet.worlds.push_back(world);
            fleet.counters.emplace_back();
            std::atomic<bool> *flag = fleet.ended[w].get();
            for(int i = 0; i < perWorld; i++)
            {
                auto system = std::make_unique<CountingSystem>(&log, prefix + std::to_string(w) + "-s" + std::to_string(i));
                if(customize) customize(world, w, i, *system);
                auto previous = system->onUpdate;
                system->onUpdate = [previous, flag] {
                    endMark.flag = flag;
                    if(previous) previous();
                };
                fleet.counters.back().push_back(system->counters);
                world->System(std::move(system));
            }
        }
        for(auto *world : fleet.worlds) world->Start(interval);
        const auto limit = std::chrono::seconds(20 * kSanitizerFactor);
        for(const auto &perWorldCounters : fleet.counters)
        {
            for(const auto &c : perWorldCounters)
            {
                waitUntil([&] { return c->update.load() >= 1; }, limit);
            }
        }
    }

    bool fleetEnded(const Fleet &fleet)
    {
        for(const auto &flag : fleet.ended)
        {
            if(!flag->load()) return false;
        }
        return true;
    }

    // Every system was started once and notified once, last registered first within its world.
    bool fleetNotifiedOnce(const Fleet &fleet)
    {
        for(const auto &perWorldCounters : fleet.counters)
        {
            for(size_t i = 0; i < perWorldCounters.size(); i++)
            {
                const auto &c = perWorldCounters[i];
                if(c->initialize.load() != 1 || c->shutdown.load() != 1) return false;
                if(i > 0 && c->shutdownSequence.load() >= perWorldCounters[i - 1]->shutdownSequence.load()) return false;
            }
        }
        return true;
    }

    long fleetUpdates(const Fleet &fleet)
    {
        long total = 0;
        for(const auto &perWorldCounters : fleet.counters)
        {
            for(const auto &c : perWorldCounters) total += c->update.load();
        }
        return total;
    }

    bool fleetAllNotified(const Fleet &fleet)
    {
        for(const auto &perWorldCounters : fleet.counters)
        {
            for(const auto &c : perWorldCounters)
            {
                if(c->shutdown.load() < 1) return false;
            }
        }
        return true;
    }

    nlohmann::json messageBetween(const std::string &container, const std::string &system)
    {
        nlohmann::json message;
        message["destination"]["container"] = container;
        message["destination"]["system"] = system;
        return message;
    }

    // What became of the sends one test made through a manager.
    struct SendTally
    {
        std::atomic<int> delivered{0};
        std::atomic<int> refused{0};
        std::atomic<int> other{0};
        std::atomic<long> longestMs{0};

        int attempts() const { return this->delivered.load() + this->refused.load() + this->other.load(); }
    };

    // A send is delivered or refused with std::runtime_error; anything else is counted as "other".
    void trySend(ecs::Manager *manager, const std::shared_ptr<SendTally> &tally, const std::string &container,
                 const std::string &system)
    {
        const auto begin = Clock::now();
        try
        {
            manager->MessageSubmit(messageBetween(container, system));
            tally->delivered++;
        }
        catch(const std::runtime_error &)
        {
            tally->refused++;
        }
        catch(...)
        {
            tally->other++;
        }
        const long took = millisecondsSince(begin);
        long seen = tally->longestMs.load();
        while(took > seen && !tally->longestMs.compare_exchange_weak(seen, took)) {}
    }

    struct ShutdownRuns
    {
        int repetitions = 0;
        int allEnded = 0;
        int notifiedOnce = 0;
        int frozen = 0;
        int notRunning = 0;
    };

    ShutdownRuns managerShutdownRuns(int repetitions)
    {
        ShutdownRuns out;
        for(int rep = 0; rep < repetitions; rep++)
        {
            EventLog log;
            Fleet fleet;
            ecs::Manager manager;
            fleetBuild(fleet, manager, log, 4, 2, 200, "w");
            manager.Shutdown();

            out.repetitions++;
            if(fleetEnded(fleet)) out.allEnded++;
            if(fleetNotifiedOnce(fleet)) out.notifiedOnce++;
            if(!manager.IsRunning()) out.notRunning++;
            const long updates = fleetUpdates(fleet);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if(fleetUpdates(fleet) == updates) out.frozen++;
        }
        return out;
    }
}

TEST_CASE("Manager shutdown stops every world and waits", "[Lifecycle]")
{
    const int repetitions = 500;
    const ShutdownRuns r = withTimeout([&] { return managerShutdownRuns(repetitions); }, watchdogLimit());
    CHECK(r.repetitions == repetitions);
    CHECK(r.allEnded == repetitions);
    CHECK(r.notifiedOnce == repetitions);
    CHECK(r.frozen == repetitions);
    CHECK(r.notRunning == repetitions);
}

TEST_CASE("Repeated and concurrent shutdown", "[Lifecycle]")
{
    SECTION("twice in a row")
    {
        const bool ok = withTimeout([] {
            EventLog log;
            Fleet fleet;
            ecs::Manager manager;
            fleetBuild(fleet, manager, log, 4, 2, 200, "w");
            manager.Shutdown();
            const bool first = fleetEnded(fleet) && fleetNotifiedOnce(fleet);
            manager.Shutdown();
            return first && fleetEnded(fleet) && fleetNotifiedOnce(fleet);
        }, watchdogLimit());
        CHECK(ok);
    }

    SECTION("from four threads at once")
    {
        const int repetitions = 50;
        const int good = withTimeout([&] {
            int count = 0;
            for(int rep = 0; rep < repetitions; rep++)
            {
                EventLog log;
                Fleet fleet;
                ecs::Manager manager;
                fleetBuild(fleet, manager, log, 4, 2, 200, "w");
                std::atomic<int> arrived{0};
                std::atomic<int> returnedComplete{0};
                std::vector<std::thread> callers;
                for(int i = 0; i < 4; i++)
                {
                    callers.emplace_back([&] {
                        arrived++;
                        waitUntil([&] { return arrived.load() == 4; }, std::chrono::seconds(20 * kSanitizerFactor));
                        manager.Shutdown();
                        if(fleetEnded(fleet) && fleetAllNotified(fleet)) returnedComplete++;
                    });
                }
                for(auto &caller : callers) caller.join();
                if(returnedComplete.load() == 4 && fleetNotifiedOnce(fleet)) count++;
            }
            return count;
        }, watchdogLimit());
        CHECK(good == repetitions);
    }
}

TEST_CASE("Manager shutdown time with a long interval", "[Lifecycle]")
{
    const long took = withTimeout([] {
        EventLog log;
        Fleet fleet;
        ecs::Manager manager;
        // 5 s between passes.
        fleetBuild(fleet, manager, log, 4, 1, 5000000, "w");
        const auto begin = Clock::now();
        manager.Shutdown();
        const long ms = millisecondsSince(begin);
        return fleetEnded(fleet) && fleetNotifiedOnce(fleet) ? ms : 1000000L;
    }, watchdogLimit());
    CHECK(took < 250 * kSanitizerFactor);
}

TEST_CASE("Caller-driven worlds are not stopped by the manager", "[Lifecycle]")
{
    const auto result = withTimeout([] {
        struct Result
        {
            int notifiedBefore = -1;
            int notifiedAfterStop = -1;
            bool stopOnThisThread = false;
            bool noThreadAfterStart = false;
            bool startThrew = false;
        } out;
        EventLog log;
        ecs::Manager manager;
        auto *world = manager.Container("driven");
        auto counters = addSystems(world, log, 2);
        passes(world, 3);
        manager.Shutdown();
        out.notifiedBefore = counters[0]->shutdown.load() + counters[1]->shutdown.load();

        const int updates = counters[0]->update.load();
        const auto threadBefore = counters[0]->updateThread();
        try
        {
            world->Start();
        }
        catch(...)
        {
            out.startThrew = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        out.noThreadAfterStart = counters[0]->update.load() == updates && counters[0]->updateThread() == threadBefore;

        world->Stop();
        out.notifiedAfterStop = counters[0]->shutdown.load() + counters[1]->shutdown.load();
        out.stopOnThisThread = counters[0]->shutdownThread() == std::this_thread::get_id()
            && counters[1]->shutdownThread() == std::this_thread::get_id();
        return out;
    }, watchdogLimit());
    CHECK(result.notifiedBefore == 0);
    CHECK_FALSE(result.startThrew);
    CHECK(result.noThreadAfterStart);
    CHECK(result.notifiedAfterStop == 2);
    CHECK(result.stopOnThisThread);
}

namespace
{
    struct SelfShutdowns
    {
        int repetitions = 0;
        int returnedAtOnce = 0;
        int notNested = 0;
        int stoppedAfterPass = 0;
        int notifiedOnce = 0;
        int ended = 0;
    };

    // One world whose system asks its own manager to shut down from its update.
    SelfShutdowns shutdownFromOwnSystem(int repetitions)
    {
        SelfShutdowns out;
        for(int rep = 0; rep < repetitions; rep++)
        {
            EventLog log;
            Fleet fleet;
            ecs::Manager manager;
            auto armed = std::make_shared<std::atomic<bool>>(false);
            auto callMs = std::make_shared<std::atomic<long>>(-1);
            auto updatesAtCall = std::make_shared<std::atomic<int>>(-1);
            auto shutdownAtReturn = std::make_shared<std::atomic<int>>(-1);
            fleetBuild(fleet, manager, log, 1, 1, 500, "self", [&](ecs::Container *, int, int, CountingSystem &system) {
                auto counters = system.counters;
                ecs::Manager *raw = &manager;
                system.onUpdate = [raw, counters, armed, callMs, updatesAtCall, shutdownAtReturn] {
                    if(!armed->load() || callMs->load() >= 0) return;
                    updatesAtCall->store(counters->update.load());
                    const auto begin = Clock::now();
                    raw->Shutdown();
                    shutdownAtReturn->store(counters->shutdown.load());
                    callMs->store(millisecondsSince(begin));
                };
            });
            armed->store(true);
            waitUntil([&] { return callMs->load() >= 0; }, std::chrono::seconds(20 * kSanitizerFactor));
            // The application thread's own request completes the wait.
            manager.Shutdown();

            const auto &c = fleet.counters[0][0];
            out.repetitions++;
            if(callMs->load() >= 0 && callMs->load() < 250 * kSanitizerFactor) out.returnedAtOnce++;
            if(shutdownAtReturn->load() == 0) out.notNested++;
            if(c->update.load() == updatesAtCall->load()) out.stoppedAfterPass++;
            if(fleetNotifiedOnce(fleet)) out.notifiedOnce++;
            if(fleetEnded(fleet)) out.ended++;
        }
        return out;
    }

    // Two worlds whose systems ask for the shutdown at the same moment.
    SelfShutdowns shutdownFromTwoWorlds(int repetitions)
    {
        SelfShutdowns out;
        for(int rep = 0; rep < repetitions; rep++)
        {
            EventLog log;
            Fleet fleet;
            ecs::Manager manager;
            auto armed = std::make_shared<std::atomic<bool>>(false);
            auto arrived = std::make_shared<std::atomic<int>>(0);
            auto worst = std::make_shared<std::atomic<long>>(-1);
            auto done = std::make_shared<std::atomic<int>>(0);
            fleetBuild(fleet, manager, log, 2, 1, 500, "two", [&](ecs::Container *, int, int, CountingSystem &system) {
                ecs::Manager *raw = &manager;
                auto once = std::make_shared<std::atomic<bool>>(false);
                system.onUpdate = [raw, armed, arrived, worst, done, once] {
                    if(!armed->load() || once->exchange(true)) return;
                    arrived->fetch_add(1);
                    waitUntil([&] { return arrived->load() == 2; }, std::chrono::seconds(20 * kSanitizerFactor));
                    const auto begin = Clock::now();
                    raw->Shutdown();
                    const long took = millisecondsSince(begin);
                    long seen = worst->load();
                    while(took > seen && !worst->compare_exchange_weak(seen, took)) {}
                    done->fetch_add(1);
                };
            });
            armed->store(true);
            waitUntil([&] { return done->load() == 2; }, std::chrono::seconds(20 * kSanitizerFactor));
            manager.Shutdown();

            out.repetitions++;
            if(done->load() == 2 && worst->load() < 250 * kSanitizerFactor) out.returnedAtOnce++;
            if(fleetNotifiedOnce(fleet)) out.notifiedOnce++;
            if(fleetEnded(fleet)) out.ended++;
        }
        return out;
    }

    struct CrossShutdown
    {
        long callMs = -1;
        bool returnedBeforeEnd = false;
        bool finishedAfterWait = false;
        bool notifiedOnce = false;
    };

    // A system of one world asks for the shutdown while another world's notification takes a while.
    CrossShutdown shutdownOfOtherWorld()
    {
        CrossShutdown out;
        EventLog log;
        Fleet fleet;
        ecs::Manager manager;
        auto armed = std::make_shared<std::atomic<bool>>(false);
        auto finished = std::make_shared<std::atomic<bool>>(false);
        auto callMs = std::make_shared<std::atomic<long>>(-1);
        auto beforeEnd = std::make_shared<std::atomic<int>>(-1);
        fleetBuild(fleet, manager, log, 2, 1, 1000000, "cross", [&](ecs::Container *, int w, int, CountingSystem &system) {
            if(w == 0)
            {
                ecs::Manager *raw = &manager;
                system.onUpdate = [raw, armed, finished, callMs, beforeEnd] {
                    if(!armed->load() || callMs->load() >= 0) return;
                    const auto begin = Clock::now();
                    raw->Shutdown();
                    callMs->store(millisecondsSince(begin));
                    beforeEnd->store(finished->load() ? 0 : 1);
                };
            }
            else
            {
                system.onShutdown = [finished] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    finished->store(true);
                };
            }
        });
        // Wake world 0 so that it runs a pass now rather than after the long interval.
        armed->store(true);
        waitUntil([&] { return callMs->load() >= 0; }, std::chrono::seconds(20 * kSanitizerFactor));
        out.callMs = callMs->load();
        out.returnedBeforeEnd = beforeEnd->load() == 1;
        manager.Shutdown();
        out.finishedAfterWait = finished->load();
        out.notifiedOnce = fleetNotifiedOnce(fleet) && fleetEnded(fleet);
        return out;
    }
}

TEST_CASE("Shutdown from inside a system", "[Lifecycle]")
{
    const int repetitions = 500;

    SECTION("one world, repeated")
    {
        const SelfShutdowns r = withTimeout([&] { return shutdownFromOwnSystem(repetitions); }, watchdogLimit());
        CHECK(r.repetitions == repetitions);
        CHECK(r.returnedAtOnce == repetitions);
        CHECK(r.notNested == repetitions);
        CHECK(r.stoppedAfterPass == repetitions);
        CHECK(r.notifiedOnce == repetitions);
        CHECK(r.ended == repetitions);
    }

    SECTION("two worlds at the same time")
    {
        const SelfShutdowns r = withTimeout([&] { return shutdownFromTwoWorlds(repetitions); }, watchdogLimit());
        CHECK(r.repetitions == repetitions);
        CHECK(r.returnedAtOnce == repetitions);
        CHECK(r.notifiedOnce == repetitions);
        CHECK(r.ended == repetitions);
    }

    SECTION("a request about another world returns at once")
    {
        const CrossShutdown r = withTimeout([] { return shutdownOfOtherWorld(); }, watchdogLimit());
        CHECK(r.callMs >= 0);
        CHECK(r.callMs < 150 * kSanitizerFactor);
        CHECK(r.returnedBeforeEnd);
        CHECK(r.finishedAfterWait);
        CHECK(r.notifiedOnce);
    }
}

TEST_CASE("A failing world thread stops the others", "[Lifecycle]")
{
    struct Result
    {
        bool allNotified = false;
        bool notRunning = false;
        int errorLines = 0;
        bool notifiedOnce = false;
        bool ended = false;
    };
    const Result r = withTimeout([] {
        Result out;
        EventLog log;
        auto sink = std::make_shared<LogSink>();
        Fleet fleet;
        ecs::Manager manager;
        auto armed = std::make_shared<std::atomic<bool>>(false);
        fleetBuild(fleet, manager, log, 3, 2, 500, "fail", [&](ecs::Container *, int w, int i, CountingSystem &system) {
            if(w == 0 && i == 1)
            {
                system.onUpdate = [armed] {
                    if(armed->load()) throw std::runtime_error("update failed on purpose");
                };
            }
        });
        sinkInstall(fleet.worlds[0], sink);
        armed->store(true);
        // Nobody asks for the shutdown: the failing world does.
        out.allNotified = waitUntil([&] { return fleetAllNotified(fleet); }, std::chrono::seconds(20 * kSanitizerFactor));
        out.notRunning = !manager.IsRunning();
        for(const auto &line : sink->snapshot())
        {
            if(line.second == "error") out.errorLines++;
        }
        manager.Shutdown();
        out.notifiedOnce = fleetNotifiedOnce(fleet);
        out.ended = fleetEnded(fleet);
        return out;
    }, watchdogLimit());
    CHECK(r.allNotified);
    CHECK(r.notRunning);
    CHECK(r.errorLines >= 1);
    CHECK(r.notifiedOnce);
    CHECK(r.ended);
}

TEST_CASE("Changes and requests during teardown", "[Lifecycle]")
{
    // The notification asks its world to stop and its manager to shut down, and records how long that took.
    struct Calls
    {
        std::atomic<int> made{0};
        std::atomic<long> longestMs{0};
    };

    auto requests = [](ecs::Container *world, ecs::Manager *manager, const std::shared_ptr<Calls> &calls) {
        return [world, manager, calls] {
            const auto begin = Clock::now();
            world->Stop();
            manager->Shutdown();
            const long took = millisecondsSince(begin);
            long seen = calls->longestMs.load();
            while(took > seen && !calls->longestMs.compare_exchange_weak(seen, took)) {}
            calls->made++;
        };
    };

    SECTION("during the stop of a threaded world")
    {
        const bool ok = withTimeout([&] {
            EventLog log;
            Fleet fleet;
            ecs::Manager manager;
            auto calls = std::make_shared<Calls>();
            fleetBuild(fleet, manager, log, 1, 2, 500, "t", [&](ecs::Container *world, int, int i, CountingSystem &system) {
                if(i == 1) system.onShutdown = requests(world, &manager, calls);
            });
            fleet.worlds[0]->Stop();
            return calls->made.load() == 1 && calls->longestMs.load() < 250 * kSanitizerFactor
                && fleetNotifiedOnce(fleet) && fleetEnded(fleet);
        }, watchdogLimit());
        CHECK(ok);
    }

    SECTION("during the destruction of a manager with threaded worlds")
    {
        const bool ok = withTimeout([&] {
            EventLog log;
            Fleet fleet;
            auto calls = std::make_shared<Calls>();
            {
                ecs::Manager manager;
                fleetBuild(fleet, manager, log, 3, 2, 500, "t", [&](ecs::Container *world, int, int i, CountingSystem &system) {
                    if(i == 1) system.onShutdown = requests(world, &manager, calls);
                });
            }
            return calls->made.load() == 3 && calls->longestMs.load() < 250 * kSanitizerFactor
                && fleetNotifiedOnce(fleet) && fleetEnded(fleet);
        }, watchdogLimit());
        CHECK(ok);
    }

    SECTION("during the destruction of a manager with a caller-driven world")
    {
        const bool ok = withTimeout([&] {
            EventLog log;
            auto calls = std::make_shared<Calls>();
            std::vector<std::shared_ptr<Counters>> counters;
            {
                ecs::Manager manager;
                auto *world = manager.Container("driven");
                counters = addSystems(world, log, 3);
                static_cast<CountingSystem *>(world->Systems.at("s3").get())->onShutdown = requests(world, &manager, calls);
                passes(world, 2);
            }
            bool notified = true;
            for(const auto &c : counters) notified = notified && c->shutdown.load() == 1;
            return calls->made.load() == 1 && calls->longestMs.load() < 250 * kSanitizerFactor && notified;
        }, watchdogLimit());
        CHECK(ok);
    }
}

TEST_CASE("Messages to a stopping world", "[Lifecycle]")
{
    struct Result
    {
        int attempts = 0;
        int other = 0;
        long longestMs = -1;
        int updatesAfterShutdown = -1;
        bool notifiedOnce = false;
        bool ended = false;
    };
    const Result r = withTimeout([] {
        Result out;
        EventLog log;
        Fleet fleet;
        ecs::Manager manager;
        auto armed = std::make_shared<std::atomic<bool>>(false);
        auto tally = std::make_shared<SendTally>();
        auto receiverDone = std::make_shared<std::atomic<bool>>(false);
        auto violations = std::make_shared<std::atomic<int>>(0);
        ecs::Manager *raw = &manager;
        // World 0 sends to the system of world 1, which takes a while to shut down.
        fleetBuild(fleet, manager, log, 2, 1, 200, "m", [&](ecs::Container *, int w, int, CountingSystem &system) {
            if(w == 0)
            {
                system.onUpdate = [raw, armed, tally] {
                    if(armed->load()) trySend(raw, tally, "m1", "m1-s0");
                };
                system.onShutdown = [raw, armed, tally] {
                    if(!armed->load()) return;
                    for(int i = 0; i < 20; i++)
                    {
                        trySend(raw, tally, "m1", "m1-s0");
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                };
            }
            else
            {
                system.onUpdate = [receiverDone, violations] {
                    if(receiverDone->load()) violations->fetch_add(1);
                };
                system.onShutdown = [receiverDone] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    receiverDone->store(true);
                };
            }
        });
        armed->store(true);
        waitUntil([&] { return tally->attempts() >= 100; }, std::chrono::seconds(20 * kSanitizerFactor));
        manager.Shutdown();

        out.attempts = tally->attempts();
        out.other = tally->other.load();
        out.longestMs = tally->longestMs.load();
        out.updatesAfterShutdown = violations->load();
        out.notifiedOnce = fleetNotifiedOnce(fleet);
        out.ended = fleetEnded(fleet);
        return out;
    }, watchdogLimit());
    CHECK(r.attempts >= 100);
    CHECK(r.other == 0);
    CHECK(r.longestMs < 150 * kSanitizerFactor);
    CHECK(r.updatesAfterShutdown == 0);
    CHECK(r.notifiedOnce);
    CHECK(r.ended);
}

TEST_CASE("IsRunning is false immediately", "[Lifecycle]")
{
    struct Result
    {
        bool sawFalse = false;
        bool shutdownStillRunning = false;
        long seenAfterMs = -1;
        bool notifiedOnce = false;
    };
    const Result r = withTimeout([] {
        Result out;
        EventLog log;
        Fleet fleet;
        ecs::Manager manager;
        fleetBuild(fleet, manager, log, 2, 1, 500, "poll", [&](ecs::Container *, int, int, CountingSystem &system) {
            system.onShutdown = [] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); };
        });
        std::atomic<bool> returned{false};
        const auto begin = Clock::now();
        std::thread caller([&] {
            manager.Shutdown();
            returned.store(true);
        });
        out.sawFalse = waitUntil([&] { return !manager.IsRunning(); }, std::chrono::seconds(20 * kSanitizerFactor));
        out.seenAfterMs = millisecondsSince(begin);
        out.shutdownStillRunning = !returned.load();
        caller.join();
        out.notifiedOnce = fleetNotifiedOnce(fleet);
        return out;
    }, watchdogLimit());
    CHECK(r.sawFalse);
    // The flag is visible while the call is still waiting for the notifications.
    CHECK(r.shutdownStillRunning);
    CHECK(r.seenAfterMs < 250 * kSanitizerFactor);
    CHECK(r.notifiedOnce);
}

namespace
{
    struct DestructionLoop
    {
        int iterations = 0;
        int allNotified = 0;
        int allEnded = 0;
        int unexpectedErrors = 0;
    };

    // Four worlds whose systems send each other messages all the time, then the manager goes away.
    DestructionLoop destroyInteractingWorlds(int iterations)
    {
        DestructionLoop out;
        for(int iteration = 0; iteration < iterations; iteration++)
        {
            EventLog log;
            Fleet fleet;
            auto tally = std::make_shared<SendTally>();
            auto armed = std::make_shared<std::atomic<bool>>(false);
            {
                ecs::Manager manager;
                ecs::Manager *raw = &manager;
                fleetBuild(fleet, manager, log, 4, 2, 200, "d", [&](ecs::Container *, int w, int i, CountingSystem &system) {
                    const std::string next = "d" + std::to_string((w + 1) % 4);
                    const std::string target = next + "-s" + std::to_string(i);
                    system.onUpdate = [raw, armed, tally, next, target] {
                        if(armed->load()) trySend(raw, tally, next, target);
                    };
                });
                armed->store(true);
                waitUntil([&] { return tally->attempts() >= 20; }, std::chrono::seconds(20 * kSanitizerFactor));
            }
            out.iterations++;
            if(fleetNotifiedOnce(fleet)) out.allNotified++;
            if(fleetEnded(fleet)) out.allEnded++;
            out.unexpectedErrors += tally->other.load();
        }
        return out;
    }
}

TEST_CASE("Destruction loop with interacting worlds", "[Lifecycle]")
{
    const int iterations = 500 / kThreadFactor;
    const DestructionLoop r = withTimeout([&] { return destroyInteractingWorlds(iterations); }, watchdogLimit());
    CHECK(r.iterations == iterations);
    CHECK(r.allNotified == iterations);
    CHECK(r.allEnded == iterations);
    CHECK(r.unexpectedErrors == 0);
}

TEST_CASE("A notification that sends a message during manager destruction", "[Lifecycle]")
{
    struct Result
    {
        int attempts = 0;
        int other = 0;
        bool notifiedOnce = false;
        bool drivenNotified = false;
    };
    const Result r = withTimeout([] {
        Result out;
        EventLog log;
        Fleet fleet;
        auto tally = std::make_shared<SendTally>();
        std::vector<std::shared_ptr<Counters>> driven;
        const std::vector<std::pair<std::string, std::string>> everyone = {
            {"n0", "n0-s0"}, {"n0", "n0-s1"}, {"n1", "n1-s0"}, {"n1", "n1-s1"}, {"c", "s1"}, {"c", "s2"}};
        {
            ecs::Manager manager;
            ecs::Manager *raw = &manager;
            auto sendToEveryone = [raw, tally, everyone] {
                for(const auto &[container, system] : everyone) trySend(raw, tally, container, system);
            };
            fleetBuild(fleet, manager, log, 2, 2, 500, "n", [&](ecs::Container *, int, int, CountingSystem &system) {
                system.onShutdown = sendToEveryone;
            });
            auto *world = manager.Container("c");
            driven = addSystems(world, log, 2);
            for(const char *handle : {"s1", "s2"})
            {
                static_cast<CountingSystem *>(world->Systems.at(handle).get())->onShutdown = sendToEveryone;
            }
            passes(world, 2);
        }
        out.attempts = tally->attempts();
        out.other = tally->other.load();
        out.notifiedOnce = fleetNotifiedOnce(fleet) && fleetEnded(fleet);
        out.drivenNotified = driven[0]->shutdown.load() == 1 && driven[1]->shutdown.load() == 1;
        return out;
    }, watchdogLimit());
    // Each of the six systems sent to all six: delivered to a world that still exists or refused.
    CHECK(r.attempts == 36);
    CHECK(r.other == 0);
    CHECK(r.notifiedOnce);
    CHECK(r.drivenNotified);
}

TEST_CASE("World creation while closing", "[Lifecycle]")
{
    // 1 created, 0 refused with std::runtime_error, 2 anything else, -1 not asked.
    SECTION("before the manager is closing")
    {
        const int outcome = withTimeout([] {
            EventLog log;
            Fleet fleet;
            auto outcome = std::make_shared<std::atomic<int>>(-1);
            ecs::Manager manager;
            ecs::Manager *raw = &manager;
            fleetBuild(fleet, manager, log, 1, 1, 500, "e", [&](ecs::Container *, int, int, CountingSystem &system) {
                system.onShutdown = [raw, outcome] {
                    try
                    {
                        outcome->store(raw->Container("created-in-notification") != nullptr ? 1 : 2);
                    }
                    catch(const std::runtime_error &)
                    {
                        outcome->store(0);
                    }
                    catch(...)
                    {
                        outcome->store(2);
                    }
                };
            });
            manager.Shutdown();
            return outcome->load();
        }, watchdogLimit());
        CHECK(outcome == 1);
    }

    SECTION("while the manager is being destroyed")
    {
        const int outcome = withTimeout([] {
            EventLog log;
            auto outcome = std::make_shared<std::atomic<int>>(-1);
            {
                ecs::Manager manager;
                ecs::Manager *raw = &manager;
                auto *world = manager.Container("driven");
                addSystems(world, log, 1);
                static_cast<CountingSystem *>(world->Systems.at("s1").get())->onShutdown = [raw, outcome] {
                    try
                    {
                        raw->Container("too-late");
                        outcome->store(1);
                    }
                    catch(const std::runtime_error &)
                    {
                        outcome->store(0);
                    }
                    catch(...)
                    {
                        outcome->store(2);
                    }
                };
                passes(world, 2);
            }
            return outcome->load();
        }, watchdogLimit());
        CHECK(outcome == 0);
    }
}

TEST_CASE("Destroying one world leaves the others running", "[Lifecycle]")
{
    struct Result
    {
        bool soloNotifiedOnce = false;
        bool siblingKeptRunning = false;
        int siblingNotifiedEarly = -1;
        bool siblingNotifiedOnce = false;
    };
    const Result r = withTimeout([] {
        Result out;
        EventLog log;
        ecs::Manager manager;
        auto solo = std::make_unique<ecs::Container>(&manager, "solo");
        auto *sibling = manager.Container("sibling");
        auto soloCounters = addSystems(solo.get(), log, 2);
        auto siblingSystem = std::make_unique<CountingSystem>(&log, "sibling-s0");
        auto siblingCounters = siblingSystem->counters;
        sibling->System(std::move(siblingSystem));
        solo->Start(200);
        sibling->Start(200);
        const auto limit = std::chrono::seconds(20 * kSanitizerFactor);
        waitUntil([&] { return soloCounters[0]->update.load() >= 1 && siblingCounters->update.load() >= 1; }, limit);

        solo.reset();
        out.soloNotifiedOnce = soloCounters[0]->shutdown.load() == 1 && soloCounters[1]->shutdown.load() == 1;
        const int updates = siblingCounters->update.load();
        out.siblingKeptRunning = waitUntil([&] { return siblingCounters->update.load() >= updates + 20; }, limit);
        out.siblingNotifiedEarly = siblingCounters->shutdown.load();

        manager.Shutdown();
        out.siblingNotifiedOnce = siblingCounters->shutdown.load() == 1;
        return out;
    }, watchdogLimit());
    CHECK(r.soloNotifiedOnce);
    CHECK(r.siblingKeptRunning);
    CHECK(r.siblingNotifiedEarly == 0);
    CHECK(r.siblingNotifiedOnce);
}

TEST_CASE("A notification may log during manager destruction", "[Lifecycle]")
{
    EventLog log;
    auto sink = std::make_shared<LogSink>();

    auto build = [&](ecs::Manager &manager) {
        auto *threaded = manager.Container("threaded");
        sinkInstall(threaded, sink);
        threaded->System(loggingSystem(log, "a"));
        threaded->System(loggingSystem(log, "b"));
        threaded->Start(200);
        return threaded;
    };

    SECTION("the lines from Shutdown() and from destructors reach the destination")
    {
        withTimeout([&] {
            ecs::Manager manager;
            build(manager);
            auto *driven = manager.Container("driven");
            sinkInstall(driven, sink);
            driven->System(loggingSystem(log, "c"));
            driven->System(loggingSystem(log, "d"));
            passes(driven, 2);
            waitUntil([&] { return sink->count("initializing b") >= 1; }, std::chrono::seconds(20 * kSanitizerFactor));
            return 0;
        }, watchdogLimit());

        for(const std::string handle : {"a", "b", "c", "d"})
        {
            const long down = sink->indexOf("shutting down " + handle);
            const long gone = sink->indexOf("destroying " + handle);
            CHECK(sink->count("shutting down " + handle) == 1);
            CHECK(sink->count("destroying " + handle) == 1);
            CHECK(down >= 0);
            CHECK(gone >= 0);
            CHECK(down < gone);
        }
    }

    SECTION("lines from Shutdown() reach the destination before it returns")
    {
        const int seen = withTimeout([&] {
            ecs::Manager manager;
            build(manager);
            waitUntil([&] { return sink->count("initializing b") >= 1; }, std::chrono::seconds(20 * kSanitizerFactor));
            manager.Shutdown();
            return sink->count("shutting down a") + sink->count("shutting down b");
        }, watchdogLimit());
        CHECK(seen == 2);
        CHECK(sink->count("destroying a") == 1);
        CHECK(sink->count("destroying b") == 1);
    }
}
