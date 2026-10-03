#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// The process-wide manager is never destroyed, so this program has a process of its own: it shuts that
// manager down and looks at what is left.

namespace
{
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

    struct Record
    {
        std::atomic<int> initialize{0};
        std::atomic<int> update{0};
        std::atomic<int> shutdown{0};
        std::atomic<bool> ended{false};
    };

    class RecordingSystem : public ecs::System
    {
      public:
        RecordingSystem(const std::string &handle, std::shared_ptr<Record> record)
            : ecs::System(handle), record(std::move(record))
        {
            this->Timing.SetInterval(std::chrono::microseconds(0));
        }

        nlohmann::json Export() const
        {
            nlohmann::json config;
            config["Handle"] = this->Handle;
            return config;
        }

        void Initialize() { this->record->initialize++; }

        void Update()
        {
            endMark.flag = &this->record->ended;
            this->record->update++;
        }

        void Shutdown() { this->record->shutdown++; }

      private:
        std::shared_ptr<Record> record;
    };

    // Runs fn on a worker thread and waits for it. When the time is up the worker is joined (never
    // detached) and the test fails.
    template <typename Fn>
    void withTimeout(Fn fn, std::chrono::seconds limit)
    {
        std::packaged_task<void()> task(std::move(fn));
        std::future<void> future = task.get_future();
        std::thread worker([&task] { task(); });
        const bool finished = future.wait_for(limit) == std::future_status::ready;
        worker.join();
        if(!finished) FAIL("timed out after " << limit.count() << " s");
        future.get();
    }

    bool waitUntil(const std::function<bool()> &predicate, std::chrono::seconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while(!predicate())
        {
            if(std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
}

TEST_CASE("Shutting down the process-wide manager stops its worlds", "[ProcessManager]")
{
    std::vector<std::shared_ptr<Record>> records;
    for(const char *handle : {"first", "second", "third"})
    {
        auto record = std::make_shared<Record>();
        records.push_back(record);
        auto *world = ECS->Container(handle);
        world->System(std::make_unique<RecordingSystem>(std::string(handle) + "-system", record));
        world->Start(200);
    }
    for(const auto &record : records)
    {
        REQUIRE(waitUntil([&] { return record->update.load() >= 1; }, std::chrono::seconds(20)));
    }
    CHECK(ECS->IsRunning());

    withTimeout([] { ECS->Shutdown(); }, std::chrono::seconds(60));

    CHECK_FALSE(ECS->IsRunning());
    std::vector<int> updates;
    for(const auto &record : records)
    {
        CHECK(record->ended.load());
        CHECK(record->initialize.load() == 1);
        CHECK(record->shutdown.load() == 1);
        updates.push_back(record->update.load());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for(size_t i = 0; i < records.size(); i++)
    {
        CHECK(records[i]->update.load() == updates[i]);
        CHECK(records[i]->shutdown.load() == 1);
    }
}
