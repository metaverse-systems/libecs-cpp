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
    ecs::Manager manager;
    auto *world = manager.Container("order");
    EventLog log;
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
    ecs::Manager manager;
    auto *world = manager.Container("explicit");
    EventLog log;
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
    ecs::Manager manager;
    auto *world = manager.Container("failing");
    EventLog log;
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
    ecs::Manager manager;
    auto *world = manager.Container("removal");
    EventLog log;

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
