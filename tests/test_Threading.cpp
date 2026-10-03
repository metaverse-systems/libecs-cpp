#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <latch>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    /*! Everything a RecorderSystem has handled. The entries are appended by the thread that updates the
     *  world and must be read only when that thread is quiet, joined or is the reading thread. The count
     *  may be read at any time. */
    struct Recorded
    {
        std::vector<std::pair<int, int>> entries;
        std::atomic<std::size_t> count{0};
    };

    // Every system here sets its update frequency to 0 so that it runs on every pass instead of at the
    // default rate of 30 passes per second.

    /*! Records the sender and number of every message it receives, in the order handled. */
    class RecorderSystem : public ecs::System
    {
      public:
        explicit RecorderSystem(const std::string &handle, std::shared_ptr<Recorded> recorded = std::make_shared<Recorded>())
          : recorded(std::move(recorded))
        {
            this->Handle = handle;
            this->Timing.SetFrequency(0);
        }

        void Update() override
        {
            while(!this->messages.empty())
            {
                auto message = this->messages.front();
                this->messages.pop();
                this->recorded->entries.emplace_back(message["sender"].get<int>(), message["number"].get<int>());
                this->recorded->count++;
            }
        }

        nlohmann::json Export() const override
        {
            return nlohmann::json::object();
        }

        std::shared_ptr<Recorded> recorded;
    };

    nlohmann::json message(const std::string &container, const std::string &system, int sender, int number)
    {
        nlohmann::json m;
        m["destination"]["container"] = container;
        m["destination"]["system"] = system;
        m["sender"] = sender;
        m["number"] = number;
        return m;
    }

    /*! Runs the body on its own thread and waits for it. If the limit passes first, sets the stop flag that
     *  the body and its workers poll, joins the thread, and returns false. The caller reports the failure
     *  from the test thread. An exception thrown by the body is rethrown here. */
    bool withTimeout(std::function<void()> body, std::chrono::seconds limit, std::atomic<bool> &stop)
    {
        std::promise<void> done;
        auto finished = done.get_future();
        std::thread runner([&body, &done]() {
            try
            {
                body();
                done.set_value();
            }
            catch(...)
            {
                done.set_exception(std::current_exception());
            }
        });

        bool completed = finished.wait_for(limit) == std::future_status::ready;
        if(!completed)
        {
            stop = true;
        }
        runner.join();
        if(completed)
        {
            finished.get();
        }
        return completed;
    }

    /*! Waits until the predicate is true, the stop flag is set or the limit passes. */
    template <typename Predicate>
    bool waitUntil(Predicate predicate, const std::atomic<bool> &stop, std::chrono::seconds limit)
    {
        auto end = std::chrono::steady_clock::now() + limit;
        while(!predicate())
        {
            if(stop || std::chrono::steady_clock::now() > end)
            {
                return false;
            }
            std::this_thread::sleep_for(1ms);
        }
        return true;
    }

    /*! Checks that every sender's numbers 0..count-1 appear exactly once and in increasing order, and that
     *  nothing else was recorded. Returns an empty string when all of that holds. */
    std::string exactlyOnceProblem(const std::vector<std::pair<int, int>> &entries, const std::map<int, int> &expected)
    {
        std::map<int, int> next;
        std::size_t total = 0;
        for(auto &[sender, count] : expected)
        {
            total += static_cast<std::size_t>(count);
        }
        if(entries.size() != total)
        {
            return "recorded " + std::to_string(entries.size()) + " messages, expected " + std::to_string(total);
        }
        for(auto &[sender, number] : entries)
        {
            auto found = expected.find(sender);
            if(found == expected.end())
            {
                return "message from unexpected sender " + std::to_string(sender);
            }
            int &want = next[sender];
            if(number != want)
            {
                return "sender " + std::to_string(sender) + " delivered number " + std::to_string(number) + " where " +
                       std::to_string(want) + " was due";
            }
            want++;
        }
        for(auto &[sender, count] : expected)
        {
            if(next[sender] != count)
            {
                return "sender " + std::to_string(sender) + " delivered " + std::to_string(next[sender]) + " of " +
                       std::to_string(count);
            }
        }
        return "";
    }

    /*! Sends numbered messages to another world from inside its own Update(), a burst per pass. */
    class PingSystem : public ecs::System
    {
      public:
        PingSystem(const std::string &handle,
          ecs::Manager *manager,
          std::string destinationContainer,
          std::string destinationSystem,
          int sender,
          int total,
          int perPass,
          std::atomic<bool> *done,
          std::atomic<int> *inUpdate,
          std::atomic<int> *errors)
          : manager(manager), destinationContainer(std::move(destinationContainer)),
            destinationSystem(std::move(destinationSystem)), sender(sender), total(total), perPass(perPass), done(done),
            inUpdate(inUpdate), errors(errors)
        {
            this->Handle = handle;
            this->Timing.SetFrequency(0);
        }

        void Update() override
        {
            this->inUpdate->fetch_add(1);
            if(!this->done->load())
            {
                for(int i = 0; i < this->perPass && this->next < this->total; i++)
                {
                    try
                    {
                        this->manager->MessageSubmit(message(this->destinationContainer, this->destinationSystem, this->sender, this->next));
                        this->next++;
                    }
                    catch(const std::exception &)
                    {
                        this->errors->fetch_add(1);
                        break;
                    }
                }
            }
            this->inUpdate->fetch_sub(1);
        }

        nlohmann::json Export() const override
        {
            return nlohmann::json::object();
        }

      private:
        ecs::Manager *manager;
        std::string destinationContainer;
        std::string destinationSystem;
        int sender;
        int total;
        int perPass;
        std::atomic<bool> *done;
        std::atomic<int> *inUpdate;
        std::atomic<int> *errors;
        int next = 0;
    };

    /*! Blocks inside Update() until released, and counts what it received. */
    class BlockingSystem : public ecs::System
    {
      public:
        BlockingSystem(const std::string &handle, std::latch *release, std::atomic<bool> *entered, std::atomic<std::size_t> *received)
          : release(release), entered(entered), received(received)
        {
            this->Handle = handle;
            this->Timing.SetFrequency(0);
        }

        void Update() override
        {
            this->entered->store(true);
            this->release->wait();
            while(!this->messages.empty())
            {
                this->messages.pop();
                this->received->fetch_add(1);
            }
        }

        nlohmann::json Export() const override
        {
            return nlohmann::json::object();
        }

      private:
        std::latch *release;
        std::atomic<bool> *entered;
        std::atomic<std::size_t> *received;
    };

    /*! Records the pass in which each message was handled, and complains if a handler runs while a send is
     *  in progress. Sends once, from the system named as the sender, in the first pass. */
    struct PassClock
    {
        int pass = 0;
        bool sending = false;
        bool handledWhileSending = false;
    };

    class PassSystem : public ecs::System
    {
      public:
        PassSystem(const std::string &handle, PassClock *clock, std::vector<std::string> targets)
          : clock(clock), targets(std::move(targets))
        {
            this->Handle = handle;
            this->Timing.SetFrequency(0);
        }

        void Update() override
        {
            while(!this->messages.empty())
            {
                this->messages.pop();
                this->handledInPass.push_back(this->clock->pass);
                if(this->clock->sending)
                {
                    this->clock->handledWhileSending = true;
                }
            }
            if(this->clock->pass == 1)
            {
                for(auto &target : this->targets)
                {
                    this->clock->sending = true;
                    this->Container->MessageSubmit(message(this->Container->Handle, target, 0, 0));
                    this->clock->sending = false;
                }
            }
        }

        nlohmann::json Export() const override
        {
            return nlohmann::json::object();
        }

        std::vector<int> handledInPass;

      private:
        PassClock *clock;
        std::vector<std::string> targets;
    };
}

TEST_CASE("Messages from 8 senders arrive exactly once and in order per sender", "[Threading]")
{
    constexpr int SENDERS = 8;
    constexpr int PER_SENDER = 10000;

    std::atomic<bool> stop{false};
    std::atomic<int> errors{0};
    std::string problem;
    std::size_t recordedCount = 0;

    bool completed = withTimeout(
      [&]() {
          ecs::Manager manager;
          auto container = manager.Container("world");
          auto recorder = std::make_unique<RecorderSystem>("recorder");
          auto recorded = recorder->recorded;
          container->System(std::move(recorder));

          std::latch go(SENDERS + 1);
          std::vector<std::thread> senders;
          for(int s = 0; s < SENDERS; s++)
          {
              senders.emplace_back([&, s]() {
                  go.arrive_and_wait();
                  for(int n = 0; n < PER_SENDER && !stop; n++)
                  {
                      try
                      {
                          manager.MessageSubmit(message("world", "recorder", s, n));
                      }
                      catch(const std::exception &)
                      {
                          errors++;
                      }
                  }
              });
          }

          go.arrive_and_wait();
          while(recorded->count < static_cast<std::size_t>(SENDERS) * PER_SENDER && !stop)
          {
              container->Update();
          }
          for(auto &t : senders)
          {
              t.join();
          }
          recordedCount = recorded->count;

          std::map<int, int> expected;
          for(int s = 0; s < SENDERS; s++)
          {
              expected[s] = PER_SENDER;
          }
          problem = exactlyOnceProblem(recorded->entries, expected);
      },
      60s,
      stop);

    if(!completed)
    {
        FAIL("the exchange did not finish in time; recorded " << recordedCount);
    }
    REQUIRE(errors == 0);
    REQUIRE(recordedCount == static_cast<std::size_t>(SENDERS) * PER_SENDER);
    INFO(problem);
    REQUIRE(problem.empty());
}

TEST_CASE("Two worlds messaging each other do not deadlock", "[Threading]")
{
    // Each direction carries 100,000 messages: 2 sender threads with 25,000 each, plus a system inside
    // the sending world that sends 50,000 from its own Update(). The full size is kept in every variant.
    constexpr int THREAD_SENDERS = 2;
    constexpr int PER_THREAD = 25000;
    constexpr int PER_SYSTEM = 50000;
    constexpr int SYSTEM_SENDER = 3;
    constexpr std::size_t PER_DIRECTION = THREAD_SENDERS * PER_THREAD + PER_SYSTEM;

    std::atomic<bool> stop{false};
    std::atomic<bool> done{false};
    std::atomic<int> inUpdate{0};
    std::atomic<int> errors{0};
    std::string problemA;
    std::string problemB;
    std::size_t countA = 0;
    std::size_t countB = 0;

    bool completed = withTimeout(
      [&]() {
          auto recordedA = std::make_shared<Recorded>();
          auto recordedB = std::make_shared<Recorded>();
          {
              auto manager = std::make_unique<ecs::Manager>();
              auto a = manager->Container("A");
              auto b = manager->Container("B");
              a->System(std::make_unique<RecorderSystem>("recorder", recordedA));
              b->System(std::make_unique<RecorderSystem>("recorder", recordedB));
              a->System(std::make_unique<PingSystem>("ping", manager.get(), "B", "recorder", SYSTEM_SENDER, PER_SYSTEM, 500, &done, &inUpdate, &errors));
              b->System(std::make_unique<PingSystem>("ping", manager.get(), "A", "recorder", SYSTEM_SENDER, PER_SYSTEM, 500, &done, &inUpdate, &errors));
              a->Start(100);
              b->Start(100);

              std::latch go(2 * THREAD_SENDERS + 1);
              std::vector<std::thread> senders;
              for(int direction = 0; direction < 2; direction++)
              {
                  std::string target = direction == 0 ? "B" : "A";
                  for(int s = 1; s <= THREAD_SENDERS; s++)
                  {
                      senders.emplace_back([&, target, s]() {
                          go.arrive_and_wait();
                          for(int n = 0; n < PER_THREAD && !stop; n++)
                          {
                              try
                              {
                                  manager->MessageSubmit(message(target, "recorder", s, n));
                              }
                              catch(const std::exception &)
                              {
                                  errors++;
                              }
                          }
                      });
                  }
              }
              go.arrive_and_wait();
              for(auto &t : senders)
              {
                  t.join();
              }

              waitUntil([&]() { return recordedA->count >= PER_DIRECTION && recordedB->count >= PER_DIRECTION; }, stop, 50s);

              // Make sure no system is still sending before the worlds are destroyed.
              done = true;
              while(inUpdate.load() != 0)
              {
                  std::this_thread::sleep_for(1ms);
              }
              countA = recordedA->count;
              countB = recordedB->count;
              manager.reset();
          }

          std::map<int, int> expected{{1, PER_THREAD}, {2, PER_THREAD}, {SYSTEM_SENDER, PER_SYSTEM}};
          problemA = exactlyOnceProblem(recordedA->entries, expected);
          problemB = exactlyOnceProblem(recordedB->entries, expected);
      },
      60s,
      stop);

    if(!completed)
    {
        FAIL("the exchange did not finish in time; A recorded " << countA << ", B recorded " << countB);
    }
    REQUIRE(errors == 0);
    REQUIRE(countA == PER_DIRECTION);
    REQUIRE(countB == PER_DIRECTION);
    INFO(problemA);
    REQUIRE(problemA.empty());
    INFO(problemB);
    REQUIRE(problemB.empty());
}

TEST_CASE("Manager::MessageSubmit returns without waiting for the update", "[Threading]")
{
    std::atomic<bool> stop{false};
    std::latch release(1);
    std::atomic<bool> entered{false};
    std::atomic<std::size_t> received{0};
    std::atomic<bool> stillBlocked{false};
    bool returned = false;

    {
        ecs::Manager manager;
        auto container = manager.Container("world");
        container->System(std::make_unique<BlockingSystem>("blocker", &release, &entered, &received));
        container->Start(100);

        REQUIRE(waitUntil([&]() { return entered.load(); }, stop, 10s));

        returned = withTimeout(
          [&]() {
              manager.MessageSubmit(message("world", "blocker", 0, 0));
              stillBlocked = received.load() == 0;
          },
          10s,
          stop);

        // Let the update finish so the world can be destroyed.
        release.count_down();
        if(returned)
        {
            REQUIRE(waitUntil([&]() { return received.load() == 1; }, stop, 10s));
        }
    }

    REQUIRE(returned);
    REQUIRE(stillBlocked);
}

// A message sent to the sender itself, or to a system that already ran in this pass, is handled in the
// next pass. A message sent to a system that has not run yet is handled in the same pass. A message is
// never handled inside the call that sends it.
TEST_CASE("A system messaging itself or a sibling is handled in a later update", "[Threading]")
{
    ecs::Manager manager;
    auto container = manager.Container("world");
    PassClock clock;

    auto early = std::make_unique<PassSystem>("early", &clock, std::vector<std::string>{});
    auto middle = std::make_unique<PassSystem>("middle", &clock, std::vector<std::string>{"middle", "early", "late"});
    auto late = std::make_unique<PassSystem>("late", &clock, std::vector<std::string>{});
    auto *earlyPointer = early.get();
    auto *middlePointer = middle.get();
    auto *latePointer = late.get();
    container->System(std::move(early));
    container->System(std::move(middle));
    container->System(std::move(late));

    for(int pass = 1; pass <= 3; pass++)
    {
        clock.pass = pass;
        container->Update();
    }

    REQUIRE(latePointer->handledInPass == std::vector<int>{1});
    REQUIRE(middlePointer->handledInPass == std::vector<int>{2});
    REQUIRE(earlyPointer->handledInPass == std::vector<int>{2});
    REQUIRE_FALSE(clock.handledWhileSending);
}

TEST_CASE("Messages to unknown or removed destinations are dropped safely", "[Threading]")
{
    ecs::Manager manager;
    auto container = manager.Container("world");
    auto recorded = std::make_shared<Recorded>();
    container->System(std::make_unique<RecorderSystem>("steady", recorded));

    REQUIRE_THROWS_AS(manager.MessageSubmit(message("nowhere", "steady", 0, 0)), std::runtime_error);
    REQUIRE_THROWS_AS(manager.MessageSubmit(message("world", "nobody", 0, 1)), std::runtime_error);

    // The sender is still usable afterwards.
    REQUIRE_NOTHROW(manager.MessageSubmit(message("world", "steady", 0, 2)));
    container->Update();
    REQUIRE(recorded->entries.size() == 1);
    REQUIRE(recorded->entries[0] == std::make_pair(0, 2));

    constexpr int SENDERS = 4;
    constexpr int PER_SENDER = 20000;
    std::atomic<bool> stop{false};
    std::atomic<int> thrown{0};
    std::atomic<int> otherErrors{0};
    std::atomic<int> finished{0};
    auto churned = std::make_shared<Recorded>();

    bool completed = withTimeout(
      [&]() {
          std::latch go(SENDERS + 1);
          std::vector<std::thread> senders;
          for(int s = 0; s < SENDERS; s++)
          {
              senders.emplace_back([&, s]() {
                  go.arrive_and_wait();
                  for(int n = 0; n < PER_SENDER && !stop; n++)
                  {
                      try
                      {
                          manager.MessageSubmit(message("world", "churn", s, n));
                      }
                      catch(const std::runtime_error &)
                      {
                          thrown++;
                      }
                      catch(const std::exception &)
                      {
                          otherErrors++;
                      }
                  }
                  finished++;
              });
          }

          go.arrive_and_wait();
          while(finished.load() < SENDERS && !stop)
          {
              container->System(std::make_unique<RecorderSystem>("churn", churned));
              container->Update();
              container->SystemDestroy("churn");
          }
          for(auto &t : senders)
          {
              t.join();
          }
      },
      60s,
      stop);

    if(!completed)
    {
        FAIL("the senders did not finish in time");
    }

    std::size_t sent = static_cast<std::size_t>(SENDERS) * PER_SENDER;
    std::size_t recordedCount = churned->entries.size();
    std::set<std::pair<int, int>> distinct(churned->entries.begin(), churned->entries.end());
    REQUIRE(otherErrors == 0);
    REQUIRE(distinct.size() == recordedCount);
    REQUIRE(recordedCount <= sent);
    REQUIRE(recordedCount + static_cast<std::size_t>(thrown.load()) <= sent);
}
