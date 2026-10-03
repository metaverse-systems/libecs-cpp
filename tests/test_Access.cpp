#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    /*! A component that counts its own destruction in a counter the test owns. */
    class TestComponent : public ecs::Component
    {
      public:
        explicit TestComponent(std::atomic<int> *released = nullptr) : released(released)
        {
            this->Type = "TestComponent";
        }

        nlohmann::json Export() const override
        {
            return nlohmann::json::object();
        }

        ~TestComponent() override
        {
            if(this->released != nullptr)
            {
                this->released->fetch_add(1);
            }
        }

        int Value = 0;

      private:
        std::atomic<int> *released;
    };

    /*! A different concrete kind that is stored under the same type name as TestComponent. */
    class OtherComponent : public ecs::Component
    {
      public:
        explicit OtherComponent(std::atomic<int> *released = nullptr) : released(released)
        {
            this->Type = "TestComponent";
        }

        nlohmann::json Export() const override
        {
            return nlohmann::json::object();
        }

        ~OtherComponent() override
        {
            if(this->released != nullptr)
            {
                this->released->fetch_add(1);
            }
        }

      private:
        std::atomic<int> *released;
    };

    class DerivedComponent : public TestComponent
    {
      public:
        explicit DerivedComponent(std::atomic<int> *released = nullptr) : TestComponent(released) {}
    };

    /*! The only place the tests attach a component. */
    std::shared_ptr<ecs::Component> attach(ecs::Entity *target, std::unique_ptr<ecs::Component> component)
    {
        return target->Component(std::move(component));
    }

    /*! A system that does nothing, so that a world has something to run a pass with. */
    class QuietSystem : public ecs::System
    {
      public:
        explicit QuietSystem(const std::string &handle) : ecs::System(handle)
        {
            this->Timing.SetFrequency(0);
        }

        void Update() override {}

        nlohmann::json Export() const override
        {
            return nlohmann::json::object();
        }
    };

    /*! Runs the call and returns the text of the std::runtime_error it throws; fails if it throws nothing else. */
    std::string errorOf(const std::function<void()> &call)
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

    /*! Concepts that ask whether a call with the given argument is accepted by the compiler. */
    template<class Target, class Arg>
    concept CanAttach = requires(Target &t, Arg a) { t.Component(std::move(a)); };

    template<class Target>
    concept CanAttachLvalue = requires(Target &t, std::unique_ptr<ecs::Component> &u) { t.Component(u); };

    struct Snapshot
    {
        std::vector<std::tuple<std::string, std::string, const void *>> entries;
        std::size_t types = 0;

        bool operator==(const Snapshot &other) const
        {
            return this->types == other.types && this->entries == other.entries;
        }
    };

    Snapshot snapshot(const ecs::Container *container)
    {
        Snapshot result;
        result.types = container->Components.size();
        for(const auto &[type, list] : container->Components)
        {
            for(const auto &[entity, component] : list)
            {
                result.entries.emplace_back(type, entity, component.get());
            }
        }
        std::sort(result.entries.begin(), result.entries.end());
        return result;
    }

    /*! Runs the body on its own thread and waits for it. If the limit passes first, sets the stop flag
     *  that the body polls, joins the thread and returns false. */
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

    /*! Looks up a component on every pass and, on the first pass, queues an attach for the next one. */
    class LookupSystem : public ecs::System
    {
      public:
        explicit LookupSystem(const std::string &handle) : ecs::System(handle)
        {
            this->Timing.SetFrequency(0);
        }

        void Update() override
        {
            this->seen.push_back(this->Container->ComponentHas("e1", "TestComponent"));
            if(!this->queued)
            {
                this->queued = true;
                auto *world = this->Container;
                this->Container->Defer([world]() { attach(world->Entity("e1"), std::make_unique<TestComponent>()); });
            }
        }

        nlohmann::json Export() const override
        {
            return nlohmann::json::object();
        }

        std::vector<bool> seen;
        bool queued = false;
    };

    std::vector<uint8_t> bytes(std::size_t size)
    {
        std::vector<uint8_t> v(size);
        for(std::size_t i = 0; i < size; i++)
        {
            v[i] = static_cast<uint8_t>(i * 31 + 7);
        }
        return v;
    }

    template<class R>
    concept CanWrite = requires(R &r) { r.Data.push_back(0); };
}

TEST_CASE("Lookup returns the stored component as its concrete kind", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");
    auto *entity = world->Entity("e1");
    auto *derivedEntity = world->Entity("e2");
    auto stored = attach(entity, std::make_unique<TestComponent>());
    auto storedDerived = attach(derivedEntity, std::make_unique<DerivedComponent>());

    SECTION("through the world")
    {
        auto found = world->ComponentGet<TestComponent>("e1", "TestComponent");
        REQUIRE(found);
        REQUIRE(found.get() == world->Components.at("TestComponent").at("e1").get());
        REQUIRE(found.get() == dynamic_cast<TestComponent *>(stored.get()));

        auto plain = world->ComponentGet("e1", "TestComponent");
        REQUIRE(plain);
        REQUIRE(plain.get() == stored.get());

        REQUIRE(world->ComponentHas("e1", "TestComponent"));
    }

    SECTION("through the entity")
    {
        auto found = entity->ComponentGet<TestComponent>("TestComponent");
        REQUIRE(found);
        REQUIRE(found.get() == world->Components.at("TestComponent").at("e1").get());

        auto plain = entity->ComponentGet("TestComponent");
        REQUIRE(plain);
        REQUIRE(plain.get() == stored.get());

        REQUIRE(entity->ComponentHas("TestComponent"));
    }

    SECTION("a derived kind is found as its base and as itself, not the other way round")
    {
        REQUIRE(world->ComponentGet<TestComponent>("e2", "TestComponent").get() == storedDerived.get());
        REQUIRE(world->ComponentGet<DerivedComponent>("e2", "TestComponent").get() == storedDerived.get());
        REQUIRE_FALSE(world->ComponentGet<DerivedComponent>("e1", "TestComponent"));
        REQUIRE(world->ComponentHas("e1", "TestComponent"));
    }
}

TEST_CASE("Lookup of an absent component is empty and changes nothing", "[Access]")
{
    const int row = GENERATE(range(0, 9));

    ecs::Manager manager;
    auto *world = manager.Container("world");
    auto *e1 = world->Entity("e1");
    auto *e2 = world->Entity("e2");
    auto *e3 = world->Entity("e3");
    auto *e5 = world->Entity("e5");
    std::atomic<int> released{0};
    auto stored = attach(e1, std::make_unique<TestComponent>(&released));
    auto other = attach(e3, std::make_unique<OtherComponent>(&released));

    std::string entity = "e2";
    std::string type = "TestComponent";
    ecs::Entity *viaEntity = e2;
    bool expectHas = false;

    switch(row)
    {
        case 0: // an entity without a component of the type
            break;
        case 1: // a type never used
            entity = "e1";
            type = "NeverUsed";
            viaEntity = e1;
            break;
        case 2: // an unknown entity handle
            entity = "nobody";
            viaEntity = nullptr;
            break;
        case 3: // an empty entity handle
            entity = "";
            viaEntity = nullptr;
            break;
        case 4: // an empty type string
            entity = "e1";
            type = "";
            viaEntity = e1;
            break;
        case 5: // a stored component of another concrete kind
            entity = "e3";
            viaEntity = e3;
            expectHas = true;
            break;
        case 6: // a null placeholder entry
            world->Components["TestComponent"]["e5"] = nullptr;
            entity = "e5";
            viaEntity = e5;
            break;
        case 7: // the entity was destroyed
            world->EntityDestroy("e1");
            entity = "e1";
            viaEntity = nullptr;
            break;
        case 8: // the component was destroyed
            world->ComponentDestroy("e1", "TestComponent");
            entity = "e1";
            viaEntity = e1;
            break;
    }

    const auto before = snapshot(world);

    CAPTURE(row, entity, type);
    REQUIRE_FALSE(world->ComponentGet<TestComponent>(entity, type));
    REQUIRE((world->ComponentGet(entity, type) != nullptr) == expectHas);
    REQUIRE(world->ComponentHas(entity, type) == expectHas);
    if(viaEntity != nullptr)
    {
        REQUIRE_FALSE(viaEntity->ComponentGet<TestComponent>(type));
        REQUIRE(viaEntity->ComponentHas(type) == expectHas);
    }

    REQUIRE(snapshot(world) == before);
    REQUIRE(world->Components.size() == before.types);

    if(row == 5)
    {
        REQUIRE(world->Components.at("TestComponent").at("e3").get() == other.get());
        REQUIRE(released.load() == 0);
    }
    if(row == 6)
    {
        auto exported = e5->Export();
        REQUIRE((!exported.contains("Components") || !exported["Components"].contains("TestComponent")));
    }
}

TEST_CASE("A type never used is not created by a lookup", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");
    auto *entity = world->Entity("e1");
    attach(entity, std::make_unique<TestComponent>());

    REQUIRE_FALSE(world->ComponentGet<TestComponent>("e1", "NeverUsed"));
    REQUIRE_FALSE(world->ComponentHas("e1", "NeverUsed"));
    REQUIRE_FALSE(entity->ComponentGet<TestComponent>("NeverUsed"));
    REQUIRE_FALSE(entity->ComponentHas("NeverUsed"));

    for(const auto &[type, list] : world->Components)
    {
        REQUIRE(type != "NeverUsed");
    }
    REQUIRE_FALSE(world->Components.contains("NeverUsed"));
    REQUIRE(world->Components.size() == 1);
}

TEST_CASE("Ten thousand mixed lookups leave the table identical", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");
    auto *e1 = world->Entity("e1");
    auto *e3 = world->Entity("e3");
    world->Entity("e2");
    attach(e1, std::make_unique<TestComponent>());
    attach(e3, std::make_unique<OtherComponent>());

    const auto before = snapshot(world);
    int found = 0;
    for(int i = 0; i < 10000; i++)
    {
        switch(i % 5)
        {
            case 0:
                found += world->ComponentGet<TestComponent>("e1", "TestComponent") ? 1 : 0;
                break;
            case 1:
                found += world->ComponentHas("e2", "TestComponent") ? 1 : 0;
                break;
            case 2:
                found += world->ComponentGet<TestComponent>("e1", "NeverUsed") ? 1 : 0;
                break;
            case 3:
                found += world->ComponentGet<TestComponent>("nobody", "TestComponent") ? 1 : 0;
                break;
            case 4:
                found += world->ComponentGet<TestComponent>("e3", "TestComponent") ? 1 : 0;
                break;
        }
    }

    REQUIRE(found == 2000);
    REQUIRE(snapshot(world) == before);
}

TEST_CASE("A holder survives replacement and destruction", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");
    auto *entity = world->Entity("e1");
    std::atomic<int> firstReleased{0};
    std::atomic<int> secondReleased{0};

    attach(entity, std::make_unique<TestComponent>(&firstReleased));
    auto held = world->ComponentGet<TestComponent>("e1", "TestComponent");
    REQUIRE(held);
    held->Value = 42;

    attach(entity, std::make_unique<TestComponent>(&secondReleased));
    REQUIRE(world->ComponentGet<TestComponent>("e1", "TestComponent").get() != held.get());
    REQUIRE(firstReleased.load() == 0);
    REQUIRE(held->Value == 42);

    world->EntityDestroy("e1");
    REQUIRE(secondReleased.load() == 1);
    REQUIRE(firstReleased.load() == 0);
    REQUIRE(held->Value == 42);

    held.reset();
    REQUIRE(firstReleased.load() == 1);
}

TEST_CASE("Lookups from a pass see the table as the pass sees it", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");
    world->Entity("e1");
    auto *system = static_cast<LookupSystem *>(world->System(std::make_unique<LookupSystem>("lookup")));

    world->Update();
    world->Update();

    REQUIRE(system->seen.size() == 2);
    REQUIRE_FALSE(system->seen[0]);
    REQUIRE(system->seen[1]);
}

TEST_CASE("Resource retrieval shares the stored data", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");

    ecs::Resource resource{bytes(1 << 20)};
    const uint8_t *buffer = resource.Data.data();
    world->ResourceAdd("big", std::move(resource));

    auto first = world->ResourceGet("big");
    REQUIRE(first);
    REQUIRE(first->Data.size() == (1u << 20));
    REQUIRE(first->Data.data() == buffer);

    for(int i = 0; i < 1000; i++)
    {
        auto again = world->ResourceGet("big");
        REQUIRE(again.get() == first.get());
        REQUIRE(again->Data.data() == buffer);
    }

    static_assert(std::is_same_v<decltype(world->ResourceGet(std::string())), std::shared_ptr<const ecs::Resource>>);
}

TEST_CASE("Resource add by lvalue copies once", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");

    ecs::Resource resource{bytes(4096)};
    const auto original = resource.Data;
    const uint8_t *buffer = resource.Data.data();
    world->ResourceAdd("copy", resource);

    REQUIRE(resource.Data == original);
    REQUIRE(resource.Data.data() == buffer);
    auto stored = world->ResourceGet("copy");
    REQUIRE(stored);
    REQUIRE(stored->Data == original);
    REQUIRE(stored->Data.data() != buffer);
}

TEST_CASE("Unknown resource is empty and creates nothing", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");
    world->ResourceAdd("known", ecs::Resource{bytes(8)});

    const auto before = world->Export();
    std::shared_ptr<const ecs::Resource> found;
    REQUIRE_NOTHROW(found = world->ResourceGet("missing"));
    REQUIRE_FALSE(found);
    REQUIRE_NOTHROW(found = world->ResourceGet(""));
    REQUIRE_FALSE(found);
    REQUIRE(world->Export() == before);

    REQUIRE_FALSE(world->ResourceGet("missing"));
    REQUIRE(world->ResourceGet("known"));
}

TEST_CASE("A resource with an empty byte block is found", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");
    world->ResourceAdd("empty", ecs::Resource{});

    auto found = world->ResourceGet("empty");
    REQUIRE(found);
    REQUIRE(found->Data.empty());
}

TEST_CASE("A reader outlives replacement and teardown", "[Access]")
{
    auto manager = std::make_unique<ecs::Manager>();
    auto *world = manager->Container("world");

    const auto original = bytes(2048);
    world->ResourceAdd("data", ecs::Resource{original});
    auto held = world->ResourceGet("data");
    REQUIRE(held);

    world->ResourceAdd("data", ecs::Resource{bytes(16)});
    auto replacement = world->ResourceGet("data");
    REQUIRE(replacement);
    REQUIRE(replacement.get() != held.get());
    REQUIRE(replacement->Data.size() == 16);
    REQUIRE(held->Data == original);

    manager.reset();
    REQUIRE(held->Data == original);
    REQUIRE(replacement->Data.size() == 16);
}

TEST_CASE("Resources map sharing is unchanged", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");

    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> shared;
    shared["a"] = std::make_shared<ecs::Resource>(ecs::Resource{bytes(32)});
    world->Resources(shared);

    auto found = world->ResourceGet("a");
    REQUIRE(found);
    REQUIRE(found.get() == shared["a"].get());

    shared["a"]->Data.push_back(1);
    REQUIRE(world->ResourceGet("a")->Data.size() == 33);
}

TEST_CASE("Concurrent lookups on an unchanging world", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");
    auto *e1 = world->Entity("e1");
    world->Entity("e2");
    attach(e1, std::make_unique<TestComponent>());

    std::atomic<bool> stop{false};
    std::atomic<int> wrong{0};
    bool completed = withTimeout(
      [&]() {
          std::latch go(2);
          auto work = [&]() {
              go.arrive_and_wait();
              for(int i = 0; i < 50000 && !stop.load(); i++)
              {
                  if(!world->ComponentGet<TestComponent>("e1", "TestComponent"))
                      wrong++;
                  if(!world->ComponentHas("e1", "TestComponent"))
                      wrong++;
                  if(world->ComponentGet<TestComponent>("e2", "TestComponent"))
                      wrong++;
                  if(world->ComponentHas("e1", "NeverUsed"))
                      wrong++;
                  if(world->ComponentGet<TestComponent>("nobody", "TestComponent"))
                      wrong++;
              }
          };
          std::thread a(work);
          std::thread b(work);
          a.join();
          b.join();
      },
      30s,
      stop);

    REQUIRE(completed);
    REQUIRE(wrong.load() == 0);
    REQUIRE(world->Components.size() == 1);
}

TEST_CASE("A retrieved resource cannot be written through", "[Access]")
{
    using Retrieved = std::remove_reference_t<decltype(*std::declval<ecs::Container &>().ResourceGet(std::string()))>;

    static_assert(std::is_same_v<Retrieved, const ecs::Resource>);
    static_assert(!CanWrite<Retrieved>);
    static_assert(CanWrite<ecs::Resource>);

    SUCCEED();
}

TEST_CASE("Attach hands over sole ownership", "[Access]")
{
    std::atomic<int> released{0};

    {
        ecs::Manager manager;
        auto *world = manager.Container("world");
        auto *entity = world->Entity("e1");
        world->Entity("e2");

        SECTION("through the entity")
        {
            auto handle = std::make_unique<TestComponent>(&released);
            auto *raw = handle.get();
            std::unique_ptr<ecs::Component> given = std::move(handle);

            auto stored = entity->Component(std::move(given));
            REQUIRE_FALSE(given);
            REQUIRE(stored.get() == raw);
            REQUIRE(stored.get() == world->Components.at("TestComponent").at("e1").get());
            REQUIRE(stored->EntityHandle == "e1");
            REQUIRE(stored.use_count() == 2);
            stored.reset();
            REQUIRE(released.load() == 0);
        }

        SECTION("through the world")
        {
            auto component = std::make_unique<TestComponent>(&released);
            component->EntityHandle = "e2";
            auto *raw = component.get();
            std::unique_ptr<ecs::Component> given = std::move(component);

            auto stored = world->Component(std::move(given));
            REQUIRE_FALSE(given);
            REQUIRE(stored.get() == raw);
            REQUIRE(stored.get() == world->Components.at("TestComponent").at("e2").get());
            REQUIRE(stored.use_count() == 2);
            stored.reset();
            REQUIRE(released.load() == 0);
        }

        REQUIRE(released.load() == 0);
    }

    REQUIRE(released.load() == 1);
}

TEST_CASE("A rejected attach releases the component once and changes nothing", "[Access]")
{
    const int row = GENERATE(range(0, 4));
    const bool viaEntity = GENERATE(false, true);

    // The entity form always names an existing entity and fills in the handle itself.
    if(viaEntity && row >= 2)
    {
        SUCCEED();
        return;
    }

    ecs::Manager manager;
    auto *world = manager.Container("world");
    auto *entity = world->Entity("e1");
    std::atomic<int> released{0};
    attach(entity, std::make_unique<OtherComponent>());

    std::unique_ptr<ecs::Component> given;
    std::string phrase;
    switch(row)
    {
        case 0:
            phrase = "component is missing";
            break;
        case 1:
        {
            auto component = std::make_unique<TestComponent>(&released);
            component->Type = "";
            given = std::move(component);
            phrase = "component type is empty";
            break;
        }
        case 2:
        {
            auto component = std::make_unique<TestComponent>(&released);
            component->EntityHandle = "";
            given = std::move(component);
            phrase = "component entity handle is empty";
            break;
        }
        case 3:
        {
            auto component = std::make_unique<TestComponent>(&released);
            component->EntityHandle = "nobody";
            given = std::move(component);
            phrase = "entity \"nobody\" does not exist";
            break;
        }
    }
    const int expectedReleases = row == 0 ? 0 : 1;

    const auto before = snapshot(world);
    CAPTURE(row, viaEntity);

    std::string text = errorOf([&]() {
        if(viaEntity)
        {
            entity->Component(std::move(given));
        }
        else
        {
            world->Component(std::move(given));
        }
    });

    REQUIRE(text.find(phrase) != std::string::npos);
    REQUIRE_FALSE(given);
    REQUIRE(released.load() == expectedReleases);
    REQUIRE(snapshot(world) == before);
    REQUIRE(world->Components.size() == before.types);

    // The world is still usable.
    auto good = attach(entity, std::make_unique<TestComponent>(&released));
    REQUIRE(good);
    REQUIRE(world->ComponentGet<TestComponent>("e1", "TestComponent").get() == good.get());
}

TEST_CASE("Attaching an empty handle is the missing-component error", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");
    auto *entity = world->Entity("e1");
    const auto before = snapshot(world);

    const std::string entityPrefix = "ecs::Entity(\"e1\")::Component(): ";
    const std::string worldPrefix = "ecs::Container(\"world\")::Component(): ";

    std::string text = errorOf([&]() { entity->Component(nullptr); });
    REQUIRE(text.rfind(entityPrefix, 0) == 0);
    REQUIRE(text.find("component is missing") != std::string::npos);

    text = errorOf([&]() { world->Component(nullptr); });
    REQUIRE(text.rfind(worldPrefix, 0) == 0);
    REQUIRE(text.find("component is missing") != std::string::npos);

    auto moved = std::make_unique<TestComponent>();
    std::unique_ptr<ecs::Component> taken = std::move(moved);
    text = errorOf([&]() { entity->Component(std::move(moved)); });
    REQUIRE(text.rfind(entityPrefix, 0) == 0);
    REQUIRE(text.find("component is missing") != std::string::npos);

    text = errorOf([&]() { world->Component(std::move(moved)); });
    REQUIRE(text.rfind(worldPrefix, 0) == 0);
    REQUIRE(text.find("component is missing") != std::string::npos);

    REQUIRE(snapshot(world) == before);
}

TEST_CASE("A second component of a type replaces the first", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");
    auto *entity = world->Entity("e1");
    std::atomic<int> firstReleased{0};
    std::atomic<int> secondReleased{0};

    auto first = attach(entity, std::make_unique<TestComponent>(&firstReleased));
    auto second = attach(entity, std::make_unique<TestComponent>(&secondReleased));

    REQUIRE(first.get() != second.get());
    REQUIRE(world->ComponentGet("e1", "TestComponent").get() == second.get());
    REQUIRE(world->Components.at("TestComponent").size() == 1);

    // The first one is still alive while somebody holds it.
    REQUIRE(firstReleased.load() == 0);
    REQUIRE(first->EntityHandle == "e1");
    first.reset();
    REQUIRE(firstReleased.load() == 1);
    REQUIRE(secondReleased.load() == 0);
}

TEST_CASE("A derived kind round-trips", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");
    auto *entity = world->Entity("e1");
    std::atomic<int> released{0};

    auto stored = attach(entity, std::make_unique<DerivedComponent>(&released));

    auto asDerived = world->ComponentGet<DerivedComponent>("e1", "TestComponent");
    auto asBase = world->ComponentGet<TestComponent>("e1", "TestComponent");
    auto asPlain = world->ComponentGet("e1", "TestComponent");
    REQUIRE(asDerived);
    REQUIRE(asBase);
    REQUIRE(asPlain);
    REQUIRE(static_cast<void *>(asDerived.get()) == static_cast<void *>(stored.get()));
    REQUIRE(static_cast<ecs::Component *>(asBase.get()) == stored.get());
    REQUIRE(asPlain.get() == stored.get());

    asDerived.reset();
    asBase.reset();
    asPlain.reset();
    stored.reset();
    REQUIRE(released.load() == 0);
    world->ComponentDestroy("e1", "TestComponent");
    REQUIRE(released.load() == 1);
}

TEST_CASE("Container and Entity attach are equivalent", "[Access]")
{
    ecs::Manager manager;
    auto *viaEntity = manager.Container("one");
    auto *viaWorld = manager.Container("two");
    viaEntity->Entity("e1");
    viaWorld->Entity("e1");

    auto a = viaEntity->Entity("e1")->Component(std::make_unique<TestComponent>());
    auto direct = std::make_unique<TestComponent>();
    direct->EntityHandle = "e1";
    auto b = viaWorld->Component(std::move(direct));

    REQUIRE(a->Type == b->Type);
    REQUIRE(a->EntityHandle == b->EntityHandle);
    REQUIRE(viaEntity->Components.size() == viaWorld->Components.size());
    REQUIRE(viaEntity->Components.at("TestComponent").size() == viaWorld->Components.at("TestComponent").size());
    REQUIRE(viaEntity->Components.at("TestComponent").at("e1").get() == a.get());
    REQUIRE(viaWorld->Components.at("TestComponent").at("e1").get() == b.get());

    auto strip = [](std::string text) {
        auto at = text.find("): ");
        return text.substr(at + 3);
    };
    std::string fromEntity = errorOf([&]() {
        auto bad = std::make_unique<TestComponent>();
        bad->Type = "";
        viaEntity->Entity("e1")->Component(std::move(bad));
    });
    std::string fromWorld = errorOf([&]() {
        auto bad = std::make_unique<TestComponent>();
        bad->Type = "";
        bad->EntityHandle = "e1";
        viaWorld->Component(std::move(bad));
    });
    REQUIRE(strip(fromEntity) == strip(fromWorld));
}

TEST_CASE("Attach from a deferred function", "[Access]")
{
    ecs::Manager manager;
    auto *world = manager.Container("world");
    world->Entity("e1");
    world->System(std::make_unique<QuietSystem>("quiet"));
    std::atomic<int> released{0};

    SECTION("the component is stored when the deferred function runs")
    {
        world->Defer([&]() { world->Entity("e1")->Component(std::make_unique<TestComponent>(&released)); });
        REQUIRE_FALSE(world->ComponentHas("e1", "TestComponent"));
        world->Update();
        REQUIRE(world->ComponentHas("e1", "TestComponent"));
        REQUIRE(released.load() == 0);
    }

    SECTION("an attach naming a destroyed entity is rejected and released once")
    {
        world->Defer([&]() { world->EntityDestroy("e1"); });
        world->Defer([&]() {
            auto component = std::make_unique<TestComponent>(&released);
            component->EntityHandle = "e1";
            world->Component(std::move(component));
        });
        std::string text = errorOf([&]() { world->Update(); });
        REQUIRE(text.find("entity \"e1\" does not exist") != std::string::npos);
        REQUIRE(released.load() == 1);
        REQUIRE_FALSE(world->Components.contains("TestComponent"));
    }
}

TEST_CASE("Misuse is rejected when the program is built", "[Access]")
{
    struct Plain
    {
    };

    static_assert(CanAttach<ecs::Entity, std::unique_ptr<DerivedComponent>>);
    static_assert(CanAttach<ecs::Entity, std::unique_ptr<ecs::Component>>);
    static_assert(CanAttach<ecs::Entity, std::nullptr_t>);
    static_assert(CanAttach<ecs::Container, std::unique_ptr<DerivedComponent>>);
    static_assert(CanAttach<ecs::Container, std::unique_ptr<ecs::Component>>);
    static_assert(CanAttach<ecs::Container, std::nullptr_t>);

    static_assert(!CanAttach<ecs::Entity, ecs::Component *>);
    static_assert(!CanAttach<ecs::Entity, DerivedComponent *>);
    static_assert(!CanAttach<ecs::Entity, std::shared_ptr<ecs::Component>>);
    static_assert(!CanAttach<ecs::Entity, std::shared_ptr<DerivedComponent>>);
    static_assert(!CanAttach<ecs::Entity, const std::unique_ptr<ecs::Component> &>);
    static_assert(!CanAttachLvalue<ecs::Entity>);

    static_assert(!CanAttach<ecs::Container, ecs::Component *>);
    static_assert(!CanAttach<ecs::Container, DerivedComponent *>);
    static_assert(!CanAttach<ecs::Container, std::shared_ptr<ecs::Component>>);
    static_assert(!CanAttach<ecs::Container, std::shared_ptr<DerivedComponent>>);
    static_assert(!CanAttach<ecs::Container, const std::unique_ptr<ecs::Component> &>);
    static_assert(!CanAttachLvalue<ecs::Container>);

    // Something that is not a component at all is rejected too.
    static_assert(!CanAttach<ecs::Entity, std::unique_ptr<Plain>>);
    static_assert(!CanAttach<ecs::Container, std::unique_ptr<Plain>>);

    SUCCEED();
}
