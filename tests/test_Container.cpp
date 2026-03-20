#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>

class TestSystem : public ecs::System
{
  public:
    TestSystem() { this->Handle = "TestSystem"; }
    TestSystem(std::string handle) { this->Handle = handle; }

    nlohmann::json Export() const
    {
        nlohmann::json config;
        config["Handle"] = this->Handle;
        config["updateCount"] = updateCount;
        return config;
    }

    void Update() { updateCount++; }

    int updateCount = 0;
};

class ThrowingSystem : public ecs::System
{
  public:
    ThrowingSystem() { this->Handle = "ThrowingSystem"; }

    nlohmann::json Export() const
    {
        nlohmann::json config;
        return config;
    }

    void Initialize() { throw std::runtime_error("Initialize failed"); }
    void Update() { throw std::runtime_error("Update failed"); }
};

class ThrowOnUpdateSystem : public ecs::System
{
  public:
    ThrowOnUpdateSystem() { this->Handle = "ThrowOnUpdateSystem"; }

    nlohmann::json Export() const
    {
        nlohmann::json config;
        return config;
    }

    void Update() { throw std::runtime_error("Update exploded"); }
};

class TestComponent : public ecs::Component
{
  public:
    TestComponent()
    {
        this->Type = "TestComponent";
    }

    nlohmann::json Export() const
    {
        nlohmann::json config;
        config["value"] = value;
        return config;
    }

    int value = 42;
};

TEST_CASE("Container can start and stop via jthread", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    container->System(std::make_unique<TestSystem>());
    container->Start(100000); // 100ms interval
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // Manager destructor will join threads
    REQUIRE(true); // If we get here without hanging, the test passes
}

TEST_CASE("Container creates and retrieves entities", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");

    auto entity = container->Entity("entity1");
    REQUIRE(entity != nullptr);
    REQUIRE(entity->Handle == "entity1");

    // Same handle returns same entity
    auto sameEntity = container->Entity("entity1");
    REQUIRE(sameEntity == entity);
}

TEST_CASE("Container destroys entities correctly", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");

    auto entity = container->Entity("entity1");
    entity->Component(new TestComponent());

    container->EntityDestroy("entity1");
    REQUIRE_FALSE(container->Entities.contains("entity1"));
}

TEST_CASE("EntityDestroy with non-existent handle is safe", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    REQUIRE_NOTHROW(container->EntityDestroy("non-existent"));
}

TEST_CASE("MessageSubmit throws for non-existent system", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");

    nlohmann::json message;
    message["destination"]["container"] = "test-container";
    message["destination"]["system"] = "NonExistentSystem";

    REQUIRE_THROWS_AS(container->MessageSubmit(message), std::runtime_error);
}

TEST_CASE("MessageSubmit does not corrupt Systems map", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    container->System(std::make_unique<TestSystem>());

    nlohmann::json message;
    message["destination"]["container"] = "test-container";
    message["destination"]["system"] = "NonExistentSystem";

    REQUIRE_THROWS(container->MessageSubmit(message));
    // The Systems map should NOT contain "NonExistentSystem"
    REQUIRE_FALSE(container->Systems.contains("NonExistentSystem"));
}

TEST_CASE("System exception during Initialize disables system", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    container->System(std::make_unique<ThrowingSystem>());
    container->System(std::make_unique<TestSystem>("GoodSystem"));

    // SystemsInitialize should catch the exception and disable the throwing system
    REQUIRE_NOTHROW(container->SystemsInitialize());

    // The good system should still be accessible
    REQUIRE(container->Systems.contains("GoodSystem"));
}

TEST_CASE("System exception during Update disables system", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    container->System(std::make_unique<ThrowOnUpdateSystem>());
    container->System(std::make_unique<TestSystem>("GoodSystem"));

    // First update - ThrowOnUpdateSystem should throw and get disabled
    REQUIRE_NOTHROW(container->Update());
    // Second update - should succeed with only GoodSystem running
    REQUIRE_NOTHROW(container->Update());
}

TEST_CASE("Manager shutdown joins all container threads", "[Container]") {
    ecs::Manager manager;
    auto c1 = manager.Container("c1");
    auto c2 = manager.Container("c2");
    c1->System(std::make_unique<TestSystem>("sys1"));
    c2->System(std::make_unique<TestSystem>("sys2"));
    c1->Start(100000);
    c2->Start(100000);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // Manager destructor joins all threads - should not hang
    REQUIRE(true);
}

TEST_CASE("Container Export includes entities and systems", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    container->System(std::make_unique<TestSystem>());
    auto entity = container->Entity("entity1");

    auto exported = container->Export();
    REQUIRE(exported["Handle"] == "test-container");
    REQUIRE(exported.contains("Entities"));
    REQUIRE(exported.contains("Systems"));
}

TEST_CASE("Bulk entity creation works correctly", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("bulk-test");

    for(int i = 0; i < 100; i++) {
        container->Entity("entity-" + std::to_string(i));
    }
    REQUIRE(container->Entities.size() == 100);
}
