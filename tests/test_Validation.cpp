#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <latch>
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
    using json = nlohmann::json;

    /*! Appends every message it receives to a list it owns. The list is read only from the thread that
     *  updates the world. Updates on every pass. */
    class RecorderSystem : public ecs::System
    {
      public:
        explicit RecorderSystem(const std::string &handle,
          std::shared_ptr<std::vector<json>> received = std::make_shared<std::vector<json>>())
          : received(std::move(received))
        {
            this->Handle = handle;
            this->Timing.SetFrequency(0);
        }

        void Update() override
        {
            while(!this->messages.empty())
            {
                this->received->push_back(this->messages.front());
                this->messages.pop();
            }
        }

        json Export() const override
        {
            return json::object();
        }

        std::shared_ptr<std::vector<json>> received;
    };

    /*! Sends one malformed message through the manager on every update, then a valid one. When catching is
     *  on it counts the rejection and carries on; otherwise the error leaves Update(). */
    class SenderSystem : public ecs::System
    {
      public:
        SenderSystem(const std::string &handle, ecs::Manager *manager, bool catching)
          : manager(manager), catching(catching)
        {
            this->Handle = handle;
            this->Timing.SetFrequency(0);
        }

        void Update() override
        {
            if(this->catching)
            {
                try
                {
                    this->manager->MessageSubmit(json::object());
                }
                catch(const std::runtime_error &)
                {
                    this->rejections++;
                }
            }
            else
            {
                this->manager->MessageSubmit(json::object());
            }
            this->manager->MessageSubmit(validMessage("world", "sink", 7, this->sent++));
        }

        json Export() const override
        {
            return json::object();
        }

        static json validMessage(const std::string &container, const std::string &system, int sender, int number)
        {
            json m;
            m["destination"]["container"] = container;
            m["destination"]["system"] = system;
            m["sender"] = sender;
            m["number"] = number;
            return m;
        }

        ecs::Manager *manager;
        bool catching;
        int rejections = 0;
        int sent = 0;
    };


    /*! Lets threads send while a manager is part-way through its destruction. */
    struct DestructionGate
    {
        std::atomic<bool> entered{false};
        std::atomic<int> finished{0};
        std::atomic<int> expected{0};
    };

    /*! When it is destroyed it signals the gate, then waits until the senders have finished or a limit
     *  passes. The manager is closing by then. */
    class GatedSystem : public ecs::System
    {
      public:
        GatedSystem(const std::string &handle, std::shared_ptr<DestructionGate> gate) : gate(std::move(gate))
        {
            this->Handle = handle;
        }

        ~GatedSystem() override
        {
            this->gate->entered = true;
            auto end = std::chrono::steady_clock::now() + 10s;
            while((this->gate->expected.load() == 0 || this->gate->finished.load() < this->gate->expected.load()) &&
                  std::chrono::steady_clock::now() < end)
            {
                std::this_thread::sleep_for(1ms);
            }
        }

        json Export() const override
        {
            return json::object();
        }

        std::shared_ptr<DestructionGate> gate;
    };

    json validMessage(const std::string &container, const std::string &system, int sender = 1, int number = 1)
    {
        return SenderSystem::validMessage(container, system, sender, number);
    }

    json destination(json container, json system)
    {
        json d = json::object();
        if(!container.is_discarded()) d["container"] = container;
        if(!system.is_discarded()) d["system"] = system;
        return d;
    }

    const json absent = json::value_t::discarded;

    json withDestination(json dest)
    {
        json m;
        m["destination"] = dest;
        m["sender"] = 1;
        m["number"] = 1;
        return m;
    }

    /*! A world named "world" in its own manager with a recorder named "sink". */
    struct Fixture
    {
        ecs::Manager manager;
        ecs::Container *container = nullptr;
        std::shared_ptr<std::vector<json>> received = std::make_shared<std::vector<json>>();

        Fixture()
        {
            this->container = this->manager.Container("world");
            this->container->System(std::make_unique<RecorderSystem>("sink", this->received));
        }
    };

    /*! Runs the call, requires a std::runtime_error whose text contains the key phrase, begins with the
     *  prefix and, when given, names the type that was found. */
    void expectRejected(const std::function<void()> &call,
      const std::string &prefix,
      const std::string &phrase,
      const std::string &got = "")
    {
        std::string text;
        bool thrown = false;
        try
        {
            call();
        }
        catch(const std::runtime_error &e)
        {
            thrown = true;
            text = e.what();
        }
        REQUIRE(thrown);
        INFO("error text: " << text);
        REQUIRE(text.rfind(prefix, 0) == 0);
        REQUIRE(text.find(phrase) != std::string::npos);
        if(!got.empty())
        {
            REQUIRE(text.find("got " + got) != std::string::npos);
        }
    }

    /*! The text of the std::runtime_error thrown by the call; fails if nothing else is thrown. */
    std::string whatOf(const std::function<void()> &call)
    {
        try
        {
            call();
        }
        catch(const std::runtime_error &e)
        {
            return e.what();
        }
        FAIL("no std::runtime_error was thrown");
        return "";
    }

    /*! Runs the body on its own thread and waits for it. If the limit passes first, sets the stop flag that
     *  the body and its workers poll, joins the thread and returns false. */
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

    /*! Sends a valid message and runs one pass, then requires it to have been received. */
    void requireDelivery(Fixture &world, int number)
    {
        REQUIRE_NOTHROW(world.manager.MessageSubmit(validMessage("world", "sink", 99, number)));
        world.container->Update();
        REQUIRE_FALSE(world.received->empty());
        REQUIRE(world.received->back()["sender"] == 99);
        REQUIRE(world.received->back()["number"] == number);
    }

    enum class WorldMode
    {
        Same,     // the world rejects it with the same phrase
        Skip,     // the world does not read the field
        Accepted, // the world ignores the field and delivers the message
        Other     // the world rejects it with a different phrase
    };

    struct Row
    {
        std::string name;
        json message;
        std::string phrase;
        std::string got;
        WorldMode mode = WorldMode::Same;
        std::string worldPhrase;
    };

    const std::string NOT_OBJECT = "message must be a JSON object";
    const std::string DEST_MISSING = "message.destination is missing";
    const std::string DEST_TYPE = "message.destination must be a JSON object";
    const std::string CONTAINER_MISSING = "message.destination.container is missing";
    const std::string CONTAINER_TYPE = "message.destination.container must be text";
    const std::string CONTAINER_EMPTY = "message.destination.container is empty";
    const std::string SYSTEM_MISSING = "message.destination.system is missing";
    const std::string SYSTEM_TYPE = "message.destination.system must be text";
    const std::string SYSTEM_EMPTY = "message.destination.system is empty";

    const std::vector<Row> &malformedRows()
    {
        static const std::vector<Row> rows = [] {
            std::vector<Row> r;
            // The message is not an object.
            r.push_back({"message is text", json("hello"), NOT_OBJECT, "string"});
            r.push_back({"message is empty text", json(""), NOT_OBJECT, "string"});
            r.push_back({"message is a number", json(5), NOT_OBJECT, "number"});
            r.push_back({"message is a boolean", json(true), NOT_OBJECT, "boolean"});
            r.push_back({"message is a list", json::array({1, 2}), NOT_OBJECT, "array"});
            r.push_back({"message is null", json(nullptr), NOT_OBJECT, "null"});
            // The destination is missing or is not an object.
            r.push_back({"message is an empty object", json::object(), DEST_MISSING});
            r.push_back({"destination is absent", json{{"sender", 1}, {"number", 1}}, DEST_MISSING});
            r.push_back({"destination is text", withDestination("world"), DEST_TYPE, "string"});
            r.push_back({"destination is a number", withDestination(3), DEST_TYPE, "number"});
            r.push_back({"destination is a list", withDestination(json::array({"world", "sink"})), DEST_TYPE, "array"});
            r.push_back({"destination is null", withDestination(nullptr), DEST_TYPE, "null"});
            r.push_back({"destination is an empty object",
              withDestination(json::object()),
              CONTAINER_MISSING,
              "",
              WorldMode::Other,
              SYSTEM_MISSING});
            // The world name.
            r.push_back({"container is absent", withDestination(destination(absent, "sink")), CONTAINER_MISSING, "", WorldMode::Accepted});
            r.push_back({"container is a number", withDestination(destination(4, "sink")), CONTAINER_TYPE, "number", WorldMode::Accepted});
            r.push_back({"container is null", withDestination(destination(nullptr, "sink")), CONTAINER_TYPE, "null", WorldMode::Accepted});
            r.push_back({"container is a list", withDestination(destination(json::array({"world"}), "sink")), CONTAINER_TYPE, "array", WorldMode::Skip});
            r.push_back({"container is an object", withDestination(destination(json::object(), "sink")), CONTAINER_TYPE, "object", WorldMode::Skip});
            r.push_back({"container is empty", withDestination(destination("", "sink")), CONTAINER_EMPTY, "", WorldMode::Accepted});
            // The system name.
            r.push_back({"system is absent", withDestination(destination("world", absent)), SYSTEM_MISSING});
            r.push_back({"system is a number", withDestination(destination("world", 4)), SYSTEM_TYPE, "number"});
            r.push_back({"system is a boolean", withDestination(destination("world", false)), SYSTEM_TYPE, "boolean"});
            r.push_back({"system is null", withDestination(destination("world", nullptr)), SYSTEM_TYPE, "null"});
            r.push_back({"system is a list", withDestination(destination("world", json::array({"sink"}))), SYSTEM_TYPE, "array"});
            r.push_back({"system is an object", withDestination(destination("world", json::object())), SYSTEM_TYPE, "object"});
            r.push_back({"system is empty", withDestination(destination("world", "")), SYSTEM_EMPTY});
            return r;
        }();
        return rows;
    }

    /*! Rows for the direct path: the same rows without the ones that only the manager reads. */
    const std::vector<Row> &worldRows()
    {
        static const std::vector<Row> rows = [] {
            std::vector<Row> r;
            for(const auto &row : malformedRows())
            {
                if(row.mode != WorldMode::Skip) r.push_back(row);
            }
            return r;
        }();
        return rows;
    }

    /*! Everything about a world that a rejected call must leave alone. */
    struct Snapshot
    {
        std::vector<std::size_t> waiting;
        std::vector<std::string> worlds;
        std::vector<json> exports;

        bool operator==(const Snapshot &other) const
        {
            return this->waiting == other.waiting && this->worlds == other.worlds && this->exports == other.exports;
        }
    };

    Snapshot snapshot(ecs::Manager &manager)
    {
        Snapshot s;
        s.worlds = manager.ContainersGet();
        std::sort(s.worlds.begin(), s.worlds.end());
        for(const auto &handle : s.worlds)
        {
            auto *world = manager.Container(handle);
            s.exports.push_back(world->Export());
            std::vector<std::string> names;
            for(auto &[name, system] : world->Systems) names.push_back(name);
            std::sort(names.begin(), names.end());
            for(const auto &name : names) s.waiting.push_back(world->Systems.at(name)->MessagesWaiting());
        }
        return s;
    }
}

TEST_CASE("Manager rejects a malformed message", "[Validation]")
{
    auto row = GENERATE(from_range(malformedRows()));
    DYNAMIC_SECTION(row.name)
    {
        Fixture world;
        expectRejected([&]() { world.manager.MessageSubmit(row.message); }, "ecs::Manager::MessageSubmit()", row.phrase, row.got);
        // The manager is still usable and the next valid message is delivered.
        requireDelivery(world, 1);
    }
}

TEST_CASE("World rejects a malformed message", "[Validation]")
{
    auto row = GENERATE(from_range(worldRows()));
    DYNAMIC_SECTION(row.name)
    {
        Fixture world;
        if(row.mode == WorldMode::Accepted)
        {
            // The world name is not read on the direct path.
            REQUIRE_NOTHROW(world.container->MessageSubmit(row.message));
            world.container->Update();
            REQUIRE(world.received->size() == 1);
            REQUIRE((*world.received)[0] == row.message);
        }
        else
        {
            auto phrase = row.mode == WorldMode::Other ? row.worldPhrase : row.phrase;
            expectRejected([&]() { world.container->MessageSubmit(row.message); },
              "ecs::Container(\"world\")::MessageSubmit()",
              phrase,
              row.got);
            REQUIRE_NOTHROW(world.container->MessageSubmit(validMessage("world", "sink", 5, 6)));
            world.container->Update();
            REQUIRE(world.received->size() == 1);
            REQUIRE((*world.received)[0]["sender"] == 5);
        }
    }
}

TEST_CASE("Missing system name is not reported as missing world name", "[Validation]")
{
    Fixture world;

    auto noSystem = whatOf([&]() { world.manager.MessageSubmit(withDestination(destination("world", absent))); });
    REQUIRE(noSystem.find("destination.system") != std::string::npos);
    REQUIRE(noSystem.find("destination.container") == std::string::npos);

    auto noContainer = whatOf([&]() { world.manager.MessageSubmit(withDestination(destination(absent, "sink"))); });
    REQUIRE(noContainer.find("destination.container") != std::string::npos);
    REQUIRE(noContainer.find("destination.system") == std::string::npos);

    auto direct = whatOf([&]() { world.container->MessageSubmit(withDestination(destination("world", absent))); });
    REQUIRE(direct.find("destination.system") != std::string::npos);
    REQUIRE(direct.find("destination.container") == std::string::npos);
}

TEST_CASE("Distinct conditions give distinct text", "[Validation]")
{
    Fixture world;
    auto send = [&](const json &m) { return whatOf([&]() { world.manager.MessageSubmit(m); }); };

    SECTION("destination")
    {
        std::vector<std::string> texts{send(json("text")),
          send(json::object()),
          send(withDestination("text"))};
        REQUIRE(std::set<std::string>(texts.begin(), texts.end()).size() == texts.size());
    }
    SECTION("container")
    {
        std::vector<std::string> texts{send(withDestination(destination(absent, "sink"))),
          send(withDestination(destination(3, "sink"))),
          send(withDestination(destination("", "sink")))};
        REQUIRE(std::set<std::string>(texts.begin(), texts.end()).size() == texts.size());
    }
    SECTION("system")
    {
        std::vector<std::string> texts{send(withDestination(destination("world", absent))),
          send(withDestination(destination("world", 3))),
          send(withDestination(destination("world", "")))};
        REQUIRE(std::set<std::string>(texts.begin(), texts.end()).size() == texts.size());
    }
    SECTION("wrong type names the type that was found")
    {
        auto asNumber = send(withDestination(destination("world", 3)));
        auto asList = send(withDestination(destination("world", json::array())));
        REQUIRE(asNumber != asList);
    }
}

TEST_CASE("Rejection changes nothing", "[Validation]")
{
    Fixture world;
    auto *other = world.manager.Container("other");
    other->System(std::make_unique<RecorderSystem>("sink"));
    REQUIRE_NOTHROW(world.manager.MessageSubmit(validMessage("world", "sink", 3, 3)));
    REQUIRE_NOTHROW(world.manager.MessageSubmit(validMessage("other", "sink", 4, 4)));

    auto before = snapshot(world.manager);
    REQUIRE(before.waiting.size() == 2);

    for(const auto &row : malformedRows())
    {
        INFO(row.name);
        REQUIRE_THROWS_AS(world.manager.MessageSubmit(row.message), std::runtime_error);
        if(row.mode != WorldMode::Skip && row.mode != WorldMode::Accepted)
        {
            REQUIRE_THROWS_AS(world.container->MessageSubmit(row.message), std::runtime_error);
        }
    }

    REQUIRE(snapshot(world.manager) == before);
}

TEST_CASE("Rejection does not touch shutdown accounting", "[Validation]")
{
    SECTION("rejected sends do not keep the manager from being destroyed")
    {
        std::atomic<bool> stop{false};
        int rejected = 0;
        bool completed = withTimeout(
          [&]() {
              auto manager = std::make_unique<ecs::Manager>();
              auto *world = manager->Container("world");
              world->System(std::make_unique<RecorderSystem>("sink"));
              for(int n = 0; n < 1000; n++)
              {
                  try
                  {
                      manager->MessageSubmit(n % 2 == 0 ? json::object() : withDestination(destination(3, "sink")));
                  }
                  catch(const std::runtime_error &)
                  {
                      rejected++;
                  }
              }
              manager.reset();
          },
          5s,
          stop);
        if(!completed)
        {
            FAIL("the manager was not destroyed in time after rejected sends");
        }
        REQUIRE(rejected == 1000);
    }

    SECTION("a malformed message while the manager is being destroyed is reported as malformed")
    {
        constexpr int ROUNDS = 5;
        constexpr int SENDERS = 4;
        constexpr int PER_SENDER = 200;
        std::atomic<bool> stop{false};
        std::atomic<int> wrong{0};
        std::atomic<int> malformedSeen{0};
        std::atomic<int> refusedSeen{0};

        bool completed = withTimeout(
          [&]() {
              for(int round = 0; round < ROUNDS && !stop; round++)
              {
                  auto gate = std::make_shared<DestructionGate>();
                  auto manager = std::make_unique<ecs::Manager>();
                  manager->Container("world")->System(std::make_unique<GatedSystem>("gated", gate));
                  ecs::Manager *raw = manager.get();
                  std::vector<std::thread> senders;
                  for(int s = 0; s < SENDERS + 1; s++)
                  {
                      senders.emplace_back([&, s]() {
                          // Wait until the manager is closing: the world is being destroyed and
                          // the destruction waits for this thread to finish.
                          while(!gate->entered.load() && !stop) std::this_thread::yield();
                          for(int n = 0; n < PER_SENDER; n++)
                          {
                              try
                              {
                                  if(s == SENDERS)
                                      raw->MessageSubmit(validMessage("world", "gated", s, n));
                                  else
                                      raw->MessageSubmit(json::object());
                                  wrong++;
                              }
                              catch(const std::runtime_error &e)
                              {
                                  std::string text = e.what();
                                  if(s == SENDERS && text.find("Container world not found.") != std::string::npos)
                                      refusedSeen++;
                                  else if(s != SENDERS && text.find("message.destination is missing") != std::string::npos)
                                      malformedSeen++;
                                  else
                                      wrong++;
                              }
                          }
                          gate->finished++;
                      });
                  }
                  gate->expected = SENDERS + 1;
                  manager.reset();
                  for(auto &t : senders) t.join();
              }
          },
          30s,
          stop);
        if(!completed)
        {
            FAIL("the destruction did not finish in time");
        }
        REQUIRE(wrong == 0);
        REQUIRE(malformedSeen == ROUNDS * SENDERS * PER_SENDER);
        REQUIRE(refusedSeen == ROUNDS * PER_SENDER);
    }
}

TEST_CASE("Unknown system direct to a world names the receiving world", "[Validation]")
{
    ecs::Manager manager;
    auto *alpha = manager.Container("alpha");
    manager.Container("beta");

    auto row = GENERATE(as<std::string>{}, "no container name", "empty container name", "other container name");
    DYNAMIC_SECTION(row)
    {
        json m;
        if(row == "no container name")
            m = withDestination(destination(absent, "ghost"));
        else if(row == "empty container name")
            m = withDestination(destination("", "ghost"));
        else
            m = withDestination(destination("beta", "ghost"));

        auto text = whatOf([&]() { alpha->MessageSubmit(m); });
        REQUIRE(text.find("\"alpha\"") != std::string::npos);
        REQUIRE(text.find("ghost") != std::string::npos);
        REQUIRE(text.find("beta") == std::string::npos);
    }
}

TEST_CASE("Unknown world and unknown system through the manager", "[Validation]")
{
    Fixture world;

    SECTION("unknown world")
    {
        expectRejected([&]() { world.manager.MessageSubmit(validMessage("nowhere", "sink")); },
          "ecs::Manager::MessageSubmit()",
          "Container nowhere not found.");
    }
    SECTION("known world, unknown system")
    {
        auto text = whatOf([&]() { world.manager.MessageSubmit(validMessage("world", "ghost")); });
        REQUIRE(text.find("\"world\"") != std::string::npos);
        REQUIRE(text.find("ghost") != std::string::npos);
        REQUIRE(text.find("not found") != std::string::npos);
    }
}

TEST_CASE("Extra fields pass through", "[Validation]")
{
    Fixture world;
    json m = validMessage("world", "sink", 12, 34);
    m["payload"] = json{{"name", "crate"}, {"size", 3}, {"tags", json::array({"a", "b"})}};
    m["count"] = 17;
    m["list"] = json::array({1, "two", nullptr});

    SECTION("through the manager")
    {
        world.manager.MessageSubmit(m);
    }
    SECTION("direct to the world")
    {
        world.container->MessageSubmit(m);
    }
    world.container->Update();
    REQUIRE(world.received->size() == 1);
    REQUIRE((*world.received)[0] == m);
}

TEST_CASE("System catches a rejection in its own update", "[Validation]")
{
    Fixture world;
    auto sender = std::make_unique<SenderSystem>("sender", &world.manager, true);
    auto *senderRaw = sender.get();
    world.container->System(std::move(sender));

    REQUIRE_NOTHROW(world.container->Update());
    REQUIRE(senderRaw->rejections == 1);
    REQUIRE_NOTHROW(world.container->Update());
    REQUIRE(senderRaw->rejections == 2);

    // The valid message sent after each rejection arrives.
    world.container->Update();
    REQUIRE(world.received->size() >= 2);
    REQUIRE((*world.received)[0]["sender"] == 7);
    REQUIRE((*world.received)[0]["number"] == 0);
    REQUIRE((*world.received)[1]["number"] == 1);
}

TEST_CASE("Uncaught rejection follows the existing path", "[Validation]")
{
    Fixture world;
    world.container->System(std::make_unique<SenderSystem>("sender", &world.manager, false));

    REQUIRE_THROWS_AS(world.container->Update(), std::runtime_error);

    // The world is still usable once the failing system is gone.
    world.container->SystemDestroy("sender");
    REQUIRE_NOTHROW(world.container->Update());
    requireDelivery(world, 2);
}

TEST_CASE("Concurrent invalid and valid senders", "[Validation]")
{
    constexpr int SENDERS = 8;
    constexpr int ROUNDS = 5000;

    std::atomic<bool> stop{false};
    std::atomic<int> notRejected{0};
    std::atomic<int> notDelivered{0};
    std::atomic<int> sentValid{0};
    std::size_t received = 0;
    std::set<std::pair<int, int>> unique;

    bool completed = withTimeout(
      [&]() {
          Fixture world;
          std::atomic<int> finished{0};
          std::latch go(SENDERS + 1);
          std::vector<std::thread> senders;
          for(int s = 0; s < SENDERS; s++)
          {
              senders.emplace_back([&, s]() {
                  go.arrive_and_wait();
                  for(int n = 0; n < ROUNDS && !stop; n++)
                  {
                      json bad;
                      switch((n + s) % 6)
                      {
                      case 0: bad = json::object(); break;
                      case 1: bad = json("text"); break;
                      case 2: bad = withDestination(nullptr); break;
                      case 3: bad = withDestination(destination(7, "sink")); break;
                      case 4: bad = withDestination(destination("world", absent)); break;
                      default: bad = withDestination(destination("world", "")); break;
                      }
                      try
                      {
                          world.manager.MessageSubmit(bad);
                          notRejected++;
                      }
                      catch(const std::runtime_error &)
                      {
                      }

                      try
                      {
                          world.manager.MessageSubmit(validMessage("world", "sink", s, n));
                          sentValid++;
                      }
                      catch(const std::exception &)
                      {
                          notDelivered++;
                      }
                  }
                  finished++;
              });
          }
          go.arrive_and_wait();
          while(finished.load() < SENDERS && !stop)
          {
              world.container->Update();
          }
          for(auto &t : senders) t.join();
          world.container->Update();
          received = world.received->size();
          for(const auto &m : *world.received)
          {
              unique.insert({m["sender"].get<int>(), m["number"].get<int>()});
          }
      },
      60s,
      stop);

    if(!completed)
    {
        FAIL("the senders did not finish in time");
    }
    REQUIRE(notRejected == 0);
    REQUIRE(notDelivered == 0);
    REQUIRE(sentValid == SENDERS * ROUNDS);
    REQUIRE(received == static_cast<std::size_t>(SENDERS * ROUNDS));
    REQUIRE(unique.size() == received);
}
