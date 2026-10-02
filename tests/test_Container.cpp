#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

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

TEST_CASE("System exception during Initialize propagates to caller", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    container->System(std::make_unique<ThrowingSystem>());

    REQUIRE_THROWS_AS(container->SystemsInitialize(), std::runtime_error);
}

TEST_CASE("System exception during Update propagates to caller", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    auto thrower = std::make_unique<ThrowOnUpdateSystem>();
    // Set frequency to 0 so every Update() call fires (test fixture path)
    thrower->Timing.SetFrequency(0);
    container->System(std::move(thrower));

    REQUIRE_THROWS_AS(container->Update(), std::runtime_error);
    // The system is not disabled - it runs, and throws, again
    REQUIRE_THROWS_AS(container->Update(), std::runtime_error);
}

TEST_CASE("System exception in container thread shuts down Manager", "[Container]") {
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    auto thrower = std::make_unique<ThrowOnUpdateSystem>();
    thrower->Timing.SetFrequency(0);
    container->System(std::move(thrower));
    container->Start(1000); // 1ms interval

    for(int i = 0; i < 200 && manager.IsRunning(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE_FALSE(manager.IsRunning());
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

TEST_CASE("SystemDestroy accepts the handle stored on the system itself", "[Container]") {
    // Removal must not depend on statement order: the identifier lives inside
    // the object being removed. This may already pass on older code.
    ecs::Manager manager;
    auto container = manager.Container("test-container");

    auto sysA = std::make_unique<TestSystem>("system-a-handle-longer-than-thirty-two-characters");
    auto sysB = std::make_unique<TestSystem>("system-b-handle-longer-than-thirty-two-characters");
    sysA->Timing.SetFrequency(0);
    sysB->Timing.SetFrequency(0);
    auto a = static_cast<TestSystem *>(container->System(std::move(sysA)));
    auto b = static_cast<TestSystem *>(container->System(std::move(sysB)));
    const std::string handleA = a->Handle;
    const std::string handleB = b->Handle;

    container->SystemDestroy(a->Handle);

    REQUIRE_FALSE(container->Systems.contains(handleA));
    REQUIRE(container->Systems.contains(handleB));

    // The counter lives outside the system because removed systems are released.
    REQUIRE_NOTHROW(container->Update());
    REQUIRE(b->updateCount == 1);
}

namespace
{
    // Everything the walk tests observe lives here, outside the systems,
    // because removed systems are released while the test is still running.
    struct WalkLog
    {
        std::vector<std::string> updates;
        std::vector<std::string> inits;
        std::unordered_map<std::string, int> destroyed;

        int updateCount(const std::string &name) const { return (int)std::count(updates.begin(), updates.end(), name); }
        int initCount(const std::string &name) const { return (int)std::count(inits.begin(), inits.end(), name); }
        int destroyCount(const std::string &name) const
        {
            auto found = destroyed.find(name);
            return found == destroyed.end() ? 0 : found->second;
        }
    };

    std::string walkHandle(const std::string &name)
    {
        return "walk-system-handle-longer-than-32-chars-" + name;
    }

    class WalkSystem : public ecs::System
    {
      public:
        WalkSystem(WalkLog *log, const std::string &name, const std::string &handle):
            name(name), log(log)
        {
            this->Handle = handle;
            this->Timing.SetFrequency(0);
        }
        ~WalkSystem() override { this->log->destroyed[this->name]++; }

        nlohmann::json Export() const override { return {{"Handle", this->Handle}}; }
        void Initialize() override
        {
            this->log->inits.push_back(this->name);
            if(this->onInitialize) this->onInitialize(*this);
        }
        void Update() override
        {
            this->log->updates.push_back(this->name);
            if(this->onUpdate) this->onUpdate(*this);
        }

        std::string name;
        int scratch = 0;
        std::function<void(WalkSystem &)> onUpdate;
        std::function<void(WalkSystem &)> onInitialize;

      private:
        WalkLog *log;
    };

    WalkSystem *walkAdd(ecs::Container *container, WalkLog &log, const std::string &name,
                        const std::string &handle = "")
    {
        auto system = std::make_unique<WalkSystem>(&log, name, handle.empty() ? walkHandle(name) : handle);
        return static_cast<WalkSystem *>(container->System(std::move(system)));
    }

    std::vector<std::string> walkPass(ecs::Container *container, WalkLog &log)
    {
        log.updates.clear();
        container->Update();
        return log.updates;
    }

    using Names = std::vector<std::string>;
}

TEST_CASE("Removing a later system during Update skips it this pass", "[Container]") {
    // Declared first so it outlives the manager and the systems it releases.
    WalkLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    int removals = 0;
    int destroyedWhileWalking = -1;

    auto a = walkAdd(container, log, "A");
    walkAdd(container, log, "B");
    auto c = walkAdd(container, log, "C");
    a->onUpdate = [&](WalkSystem &self) {
        if(removals++ == 0) self.Container->SystemDestroy(walkHandle("B"));
    };
    c->onUpdate = [&](WalkSystem &) {
        if(destroyedWhileWalking < 0) destroyedWhileWalking = log.destroyCount("B");
    };

    REQUIRE(walkPass(container, log) == Names{"A", "C"});
    REQUIRE(log.updateCount("B") == 0);
    // Release waits for the end of the pass.
    REQUIRE(destroyedWhileWalking == 0);
    REQUIRE(log.destroyCount("B") == 1);
    REQUIRE_FALSE(container->Systems.contains(walkHandle("B")));

    int allA = 1, allC = 1;
    for(int i = 0; i < 2; i++)
    {
        auto names = walkPass(container, log);
        allA += (int)std::count(names.begin(), names.end(), "A");
        allC += (int)std::count(names.begin(), names.end(), "C");
        REQUIRE(log.updateCount("B") == 0);
    }
    REQUIRE(allA == 3);
    REQUIRE(allC == 3);
    REQUIRE(log.destroyCount("B") == 1);
}

TEST_CASE("Removing an earlier system during Update keeps this pass's counts", "[Container]") {
    // Declared first so it outlives the manager and the systems it releases.
    WalkLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    bool removed = false;

    walkAdd(container, log, "A");
    walkAdd(container, log, "B");
    auto c = walkAdd(container, log, "C");
    c->onUpdate = [&](WalkSystem &self) {
        if(!removed)
        {
            removed = true;
            self.Container->SystemDestroy(walkHandle("A"));
        }
    };

    REQUIRE(walkPass(container, log) == Names{"A", "B", "C"});
    REQUIRE(log.destroyCount("A") == 1);
    REQUIRE(walkPass(container, log) == Names{"B", "C"});
    REQUIRE(log.destroyCount("A") == 1);
}

TEST_CASE("A system can remove itself during its own Update", "[Container]") {
    // Declared first so it outlives the manager and the systems it releases.
    WalkLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    bool removed = false;
    bool goneInsideUpdate = false;
    bool handleReadable = false;
    int destroyedInsideUpdate = -1;

    walkAdd(container, log, "A");
    auto b = walkAdd(container, log, "B");
    walkAdd(container, log, "C");
    b->onUpdate = [&](WalkSystem &self) {
        if(removed) return;
        removed = true;
        self.Container->SystemDestroy(self.Handle);
        goneInsideUpdate = !self.Container->Systems.contains(walkHandle("B"));
        handleReadable = (self.Handle == walkHandle("B"));
        self.scratch = 7;
        destroyedInsideUpdate = log.destroyCount("B");
    };

    REQUIRE(walkPass(container, log) == Names{"A", "B", "C"});
    REQUIRE(goneInsideUpdate);
    REQUIRE(handleReadable);
    REQUIRE(destroyedInsideUpdate == 0);
    REQUIRE(log.destroyCount("B") == 1);
    REQUIRE_FALSE(container->Systems.contains(walkHandle("B")));

    REQUIRE_FALSE(container->Export()["Systems"].contains(walkHandle("B")));
    nlohmann::json message = {{"destination", {{"container", "test-container"}, {"system", walkHandle("B")}}}};
    REQUIRE_THROWS_AS(container->MessageSubmit(message), std::runtime_error);

    REQUIRE(walkPass(container, log) == Names{"A", "C"});
    REQUIRE(walkPass(container, log) == Names{"A", "C"});
    REQUIRE(log.updateCount("B") == 0);
    REQUIRE(log.destroyCount("B") == 1);
}

TEST_CASE("Removing a system twice in one pass releases it once", "[Container]") {
    // Declared first so it outlives the manager and the systems it releases.
    WalkLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");

    SECTION("a sibling removed twice") {
        bool removed = false;
        auto a = walkAdd(container, log, "A");
        walkAdd(container, log, "B");
        walkAdd(container, log, "C");
        a->onUpdate = [&](WalkSystem &self) {
            if(removed) return;
            removed = true;
            self.Container->SystemDestroy(walkHandle("B"));
            self.Container->SystemDestroy(walkHandle("B"));
        };

        REQUIRE(walkPass(container, log) == Names{"A", "C"});
        REQUIRE(log.destroyCount("B") == 1);
        REQUIRE(walkPass(container, log) == Names{"A", "C"});
        REQUIRE(walkPass(container, log) == Names{"A", "C"});
        REQUIRE(log.updateCount("B") == 0);
        REQUIRE(log.destroyCount("B") == 1);
    }

    SECTION("a system that removes itself twice") {
        bool removed = false;
        walkAdd(container, log, "A");
        auto b = walkAdd(container, log, "B");
        walkAdd(container, log, "C");
        b->onUpdate = [&](WalkSystem &self) {
            if(removed) return;
            removed = true;
            self.Container->SystemDestroy(self.Handle);
            self.Container->SystemDestroy(self.Handle);
        };

        REQUIRE(walkPass(container, log) == Names{"A", "B", "C"});
        REQUIRE(log.destroyCount("B") == 1);
        REQUIRE(walkPass(container, log) == Names{"A", "C"});
        REQUIRE(walkPass(container, log) == Names{"A", "C"});
        REQUIRE(log.destroyCount("B") == 1);
    }
}

TEST_CASE("Removing and re-registering an identifier in one pass", "[Container]") {
    // Declared first so it outlives the manager and the systems it releases.
    WalkLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    bool done = false;
    bool pointerMatches = false;
    int oldDestroyedInside = -1;

    auto a = walkAdd(container, log, "A");
    walkAdd(container, log, "B");
    walkAdd(container, log, "C");
    a->onUpdate = [&](WalkSystem &self) {
        if(done) return;
        done = true;
        self.Container->SystemDestroy(walkHandle("B"));
        auto *replacement = walkAdd(self.Container, log, "B2", walkHandle("B"));
        pointerMatches = (self.Container->Systems.at(walkHandle("B")).get() == replacement);
        oldDestroyedInside = log.destroyCount("B");
    };

    REQUIRE(walkPass(container, log) == Names{"A", "C"});
    REQUIRE(pointerMatches);
    REQUIRE(oldDestroyedInside == 0);
    REQUIRE(log.destroyCount("B") == 1);
    REQUIRE(log.destroyCount("B2") == 0);
    REQUIRE(container->Systems.contains(walkHandle("B")));

    REQUIRE(walkPass(container, log) == Names{"A", "C", "B2"});
    REQUIRE(walkPass(container, log) == Names{"A", "C", "B2"});
    REQUIRE(log.destroyCount("B") == 1);
    REQUIRE(log.destroyCount("B2") == 0);
}

TEST_CASE("A system registered during Update runs from the next pass", "[Container]") {
    // Declared first so it outlives the manager and the systems it releases.
    WalkLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    bool done = false;
    bool findable = false;
    ecs::System *registered = nullptr;
    ecs::System *found = nullptr;

    auto a = walkAdd(container, log, "A");
    walkAdd(container, log, "B");
    a->onUpdate = [&](WalkSystem &self) {
        if(done) return;
        done = true;
        registered = walkAdd(self.Container, log, "N");
        auto it = self.Container->Systems.find(walkHandle("N"));
        findable = (it != self.Container->Systems.end());
        if(findable) found = it->second.get();
    };

    REQUIRE(walkPass(container, log) == Names{"A", "B"});
    REQUIRE(findable);
    REQUIRE(found == registered);
    REQUIRE(walkPass(container, log) == Names{"A", "B", "N"});
}

TEST_CASE("A system that fails after removing a sibling", "[Container]") {
    // Declared first so it outlives the manager and the systems it releases.
    WalkLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    bool thrown = false;
    std::vector<std::pair<std::string, std::string>> logged;
    container->LoggerSet([&](const std::string &message, const std::string &level) {
        logged.emplace_back(message, level);
    });

    auto a = walkAdd(container, log, "A");
    walkAdd(container, log, "B");
    walkAdd(container, log, "C");
    a->onUpdate = [&](WalkSystem &self) {
        if(thrown) return;
        thrown = true;
        self.Container->SystemDestroy(walkHandle("B"));
        throw std::runtime_error("deliberate failure");
    };

    log.updates.clear();
    REQUIRE_THROWS_AS(container->Update(), std::runtime_error);
    bool errorLogged = false;
    for(const auto &[message, level] : logged)
    {
        if(level == "error" && message.find(walkHandle("A")) != std::string::npos) errorLogged = true;
    }
    REQUIRE(errorLogged);
    REQUIRE_FALSE(container->Systems.contains(walkHandle("B")));
    REQUIRE(log.destroyCount("B") == 1);
    REQUIRE(log.updates == Names{"A"});

    REQUIRE_NOTHROW(walkPass(container, log));
    REQUIRE(log.updates == Names{"A", "C"});
    REQUIRE(log.destroyCount("B") == 1);
}

TEST_CASE("Duplicate registration stays memory safe during and outside a pass", "[Container]") {
    // Declared first so it outlives the manager and the systems it releases.
    WalkLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");

    SECTION("outside a pass") {
        walkAdd(container, log, "X1", walkHandle("X"));
        auto *second = walkAdd(container, log, "X2", walkHandle("X"));
        REQUIRE(log.destroyCount("X1") == 1);
        REQUIRE(container->Systems.at(walkHandle("X")).get() == second);

        // The handle has two update slots, so the replacement runs twice per pass.
        walkPass(container, log);
        REQUIRE(log.updateCount("X2") == 2);
        walkPass(container, log);
        REQUIRE(log.updateCount("X2") == 2);
        REQUIRE(log.updateCount("X1") == 0);
        REQUIRE(log.destroyCount("X1") == 1);
        REQUIRE(log.destroyCount("X2") == 0);
    }

    SECTION("during a pass") {
        bool done = false;
        ecs::System *replacement = nullptr;
        auto a = walkAdd(container, log, "A");
        walkAdd(container, log, "B");
        a->onUpdate = [&](WalkSystem &self) {
            if(done) return;
            done = true;
            replacement = walkAdd(self.Container, log, "B2", walkHandle("B"));
        };

        REQUIRE_NOTHROW(container->Update());
        REQUIRE(log.destroyCount("B") == 1);
        REQUIRE(log.destroyCount("B2") == 0);
        REQUIRE(container->Systems.at(walkHandle("B")).get() == replacement);
        REQUIRE_NOTHROW(container->Update());
        REQUIRE(log.destroyCount("B") == 1);
    }
}

TEST_CASE("Systems can be added and removed during start-up", "[Container]") {
    // Declared first so it outlives the manager and the systems it releases.
    WalkLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");

    auto a = walkAdd(container, log, "A");
    walkAdd(container, log, "B");
    walkAdd(container, log, "C");

    SECTION("an initializer removes a later system") {
        a->onInitialize = [](WalkSystem &self) { self.Container->SystemDestroy(walkHandle("B")); };

        REQUIRE_NOTHROW(container->SystemsInitialize());
        REQUIRE(log.inits == Names{"A", "C"});
        REQUIRE(log.destroyCount("B") == 1);
        REQUIRE_FALSE(container->Systems.contains(walkHandle("B")));
    }

    SECTION("an initializer removes itself") {
        bool touched = false;
        int destroyedInside = -1;
        a->onInitialize = [&](WalkSystem &self) {
            self.Container->SystemDestroy(self.Handle);
            self.scratch = 3;
            touched = (self.Handle == walkHandle("A"));
            destroyedInside = log.destroyCount("A");
        };

        REQUIRE_NOTHROW(container->SystemsInitialize());
        REQUIRE(touched);
        REQUIRE(destroyedInside == 0);
        REQUIRE(log.inits == Names{"A", "B", "C"});
        REQUIRE(log.destroyCount("A") == 1);
        REQUIRE_FALSE(container->Systems.contains(walkHandle("A")));
    }

    SECTION("an initializer registers a system") {
        bool findable = false;
        a->onInitialize = [&](WalkSystem &self) {
            walkAdd(self.Container, log, "N");
            findable = self.Container->Systems.contains(walkHandle("N"));
        };

        REQUIRE_NOTHROW(container->SystemsInitialize());
        REQUIRE(findable);
        REQUIRE(log.inits == Names{"A", "B", "C"});
        REQUIRE(log.initCount("N") == 0);
        REQUIRE(walkPass(container, log) == Names{"A", "B", "C", "N"});
    }
}

TEST_CASE("A system that fails during start-up after removing a sibling", "[Container]") {
    // Declared first so it outlives the manager and the systems it releases.
    WalkLog log;
    ecs::Manager manager;
    auto container = manager.Container("test-container");
    std::vector<std::pair<std::string, std::string>> logged;
    container->LoggerSet([&](const std::string &message, const std::string &level) {
        logged.emplace_back(message, level);
    });

    auto a = walkAdd(container, log, "A");
    walkAdd(container, log, "B");
    walkAdd(container, log, "C");
    a->onInitialize = [](WalkSystem &self) {
        self.Container->SystemDestroy(walkHandle("B"));
        throw std::runtime_error("deliberate failure");
    };

    REQUIRE_THROWS_AS(container->SystemsInitialize(), std::runtime_error);
    bool errorLogged = false;
    for(const auto &[message, level] : logged)
    {
        if(level == "error" && message.find(walkHandle("A")) != std::string::npos) errorLogged = true;
    }
    REQUIRE(errorLogged);
    REQUIRE(log.inits == Names{"A"});
    REQUIRE(log.destroyCount("B") == 1);
    REQUIRE_FALSE(container->Systems.contains(walkHandle("B")));

    REQUIRE(walkPass(container, log) == Names{"A", "C"});
}
