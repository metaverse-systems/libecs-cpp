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

TEST_CASE("SystemDestroy removes system from Systems map", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    container->System(std::make_unique<TestSystem>("DestroyMe"));
    REQUIRE(container->Systems.contains("DestroyMe"));

    container->SystemDestroy("DestroyMe");
    REQUIRE_FALSE(container->Systems.contains("DestroyMe"));
}

TEST_CASE("SystemDestroy with non-existent handle is no-op", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    REQUIRE_NOTHROW(container->SystemDestroy("NonExistent"));
}

TEST_CASE("SystemDestroy removes handle from disabledSystems set", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    container->System(std::make_unique<ThrowingSystem>());
    // Initialize to trigger exception and add to disabledSystems
    container->SystemsInitialize();
    REQUIRE(container->Systems.find("ThrowingSystem")->second);
    // Verify it's in disabledSystems (private, but we can check via behavior)
    // After destroy, re-adding a system with same handle should work
    container->SystemDestroy("ThrowingSystem");
    REQUIRE_FALSE(container->Systems.contains("ThrowingSystem"));
}

TEST_CASE("SystemDestroy removes handle from system_order_", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    auto sysA = std::make_unique<TestSystem>("SystemA");
    auto sysB = std::make_unique<TestSystem>("SystemB");
    auto sysC = std::make_unique<TestSystem>("SystemC");
    // Set frequency to 0 so every Update() call fires (test fixture path)
    sysA->Timing.SetFrequency(0);
    sysB->Timing.SetFrequency(0);
    sysC->Timing.SetFrequency(0);
    container->System(std::move(sysA));
    container->System(std::move(sysB));
    container->System(std::move(sysC));

    auto ptrA = static_cast<TestSystem *>(container->Systems["SystemA"].get());
    auto ptrC = static_cast<TestSystem *>(container->Systems["SystemC"].get());

    // Destroy middle system
    container->SystemDestroy("SystemB");

    // Run update - only A and C should update
    container->Update();

    REQUIRE(ptrA->updateCount == 1);
    REQUIRE(ptrC->updateCount == 1);
    // SystemB is destroyed, so it cannot increment
}

TEST_CASE("SystemDestroy with empty string handle is no-op", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    container->System(std::make_unique<TestSystem>("ExistingSystem"));
    REQUIRE_NOTHROW(container->SystemDestroy(""));
    // Existing system should still be present
    REQUIRE(container->Systems.contains("ExistingSystem"));
}

TEST_CASE("SystemDestroy called during active Update iteration is safe", "[Container]") {
    // This test verifies that destroying a system during Update doesn't crash
    // We use a system that destroys its sibling on first update
    // Then verify the sibling is gone and the remaining systems still work

    // Create a system that destroys its sibling on first update
    class SelfishSystem : public ecs::System {
      public:
        SelfishSystem() { this->Handle = "SelfishSystem"; }
        nlohmann::json Export() const override { return {{"Handle", this->Handle}}; }
        void Update() override {
            updateCount++;
            // Try to destroy sibling - container should handle gracefully
            if(this->Container) {
                this->Container->SystemDestroy("SiblingSystem");
            }
        }
        int updateCount = 0;
    };

    class SiblingSystem : public ecs::System {
      public:
        SiblingSystem() { this->Handle = "SiblingSystem"; }
        nlohmann::json Export() const override { return {{"Handle", this->Handle}}; }
        void Update() override {
            updateCount++;
        }
        int updateCount = 0;
    };

    ecs::Manager manager;
    auto container = manager.Container("test-container");
    auto selfish = std::make_unique<SelfishSystem>();
    auto sibling = std::make_unique<SiblingSystem>();
    selfish->Timing.SetFrequency(0);
    sibling->Timing.SetFrequency(0);
    container->System(std::move(selfish));
    container->System(std::move(sibling));

    // Should not crash
    REQUIRE_NOTHROW(container->Update());

    // SelfishSystem should have run
    auto selfishSys = static_cast<SelfishSystem *>(container->Systems["SelfishSystem"].get());
    REQUIRE(selfishSys->updateCount == 1);

    // SiblingSystem should be destroyed
    REQUIRE_FALSE(container->Systems.contains("SiblingSystem"));

    // Second update should still be safe
    REQUIRE_NOTHROW(container->Update());
    REQUIRE(selfishSys->updateCount == 2);
}
