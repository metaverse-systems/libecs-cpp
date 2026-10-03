#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>

#include <chrono>
#include <memory>
#include <string>

// Exported state is compared as whole JSON values.

namespace
{
    class CounterSystem : public ecs::System
    {
      public:
        CounterSystem(std::string handle, std::string label)
            : ecs::System(std::move(handle)), label(std::move(label))
        {
        }

        nlohmann::json Export() const
        {
            nlohmann::json config;
            config["Handle"] = this->Handle;
            config["label"] = this->label;
            config["updateCount"] = this->updateCount;
            return config;
        }

        void Update() { this->updateCount++; }

        std::string label;
        int updateCount = 0;
    };

    class PositionComponent : public ecs::Component
    {
      public:
        PositionComponent(double x, double y)
            : x(x), y(y)
        {
            this->Type = "Position";
        }

        nlohmann::json Export() const
        {
            nlohmann::json config;
            config["x"] = this->x;
            config["y"] = this->y;
            return config;
        }

        double x;
        double y;
    };

    // A component whose export carries no fields.
    class TagComponent : public ecs::Component
    {
      public:
        TagComponent() { this->Type = "Tag"; }

        nlohmann::json Export() const { return nlohmann::json::object(); }
    };

    std::unique_ptr<CounterSystem> counterMake(const std::string &handle, const std::string &label)
    {
        auto system = std::make_unique<CounterSystem>(handle, label);
        // Every Update() of the world runs the system.
        system->Timing.SetInterval(std::chrono::microseconds(0));
        return system;
    }
}

TEST_CASE("A world with no entities and no systems exports only its identifier", "[Export]") {
    ecs::Manager manager;
    auto world = manager.Container("empty-world");

    const nlohmann::json expected = {{"Handle", "empty-world"}};
    REQUIRE(world->Export() == expected);
}

TEST_CASE("A world exports its identifier, systems and entities with their components", "[Export]") {
    ecs::Manager manager;
    auto world = manager.Container("world");
    world->System(counterMake("alpha", "first"));
    world->System(counterMake("beta", "second"));

    auto mover = world->Entity("mover");
    mover->Component(std::make_unique<PositionComponent>(1.5, -2.0));
    mover->Component(std::make_unique<TagComponent>());
    world->Entity("bare");

    const nlohmann::json expected = {
        {"Handle", "world"},
        {"Systems",
         {{"alpha", {{"Handle", "alpha"}, {"label", "first"}, {"updateCount", 0}}},
          {"beta", {{"Handle", "beta"}, {"label", "second"}, {"updateCount", 0}}}}},
        {"Entities",
         {{"mover",
           {{"Handle", "mover"},
            {"Components", {{"Position", {{"x", 1.5}, {"y", -2.0}}}, {"Tag", nlohmann::json::object()}}}}},
          {"bare", {{"Handle", "bare"}}}}}};

    REQUIRE(world->Export() == expected);
}

TEST_CASE("A component's exported fields appear in the world export", "[Export]") {
    ecs::Manager manager;
    auto world = manager.Container("world");
    auto entity = world->Entity("e");
    entity->Component(std::make_unique<PositionComponent>(3.0, 4.0));

    const auto exported = world->Export();
    REQUIRE(exported["Entities"]["e"]["Components"]["Position"] == nlohmann::json({{"x", 3.0}, {"y", 4.0}}));
    REQUIRE(entity->Export()["Components"]["Position"]["x"] == 3.0);
    REQUIRE(entity->Export()["Components"]["Position"]["y"] == 4.0);
}

TEST_CASE("Two exports of the same world are equal", "[Export]") {
    ecs::Manager manager;
    auto world = manager.Container("world");
    world->System(counterMake("alpha", "first"));
    world->Entity("e")->Component(std::make_unique<PositionComponent>(1.0, 2.0));

    const auto first = world->Export();
    const auto second = world->Export();
    REQUIRE(first == second);

    world->Update();
    const auto third = world->Export();
    const auto fourth = world->Export();
    REQUIRE(third == fourth);
    REQUIRE(third != first);
}

TEST_CASE("Exporting does not change the world", "[Export]") {
    ecs::Manager manager;
    auto world = manager.Container("world");
    auto owned = counterMake("alpha", "first");
    CounterSystem *system = owned.get();
    world->System(std::move(owned));
    auto entity = world->Entity("e");
    auto position = entity->Component(std::make_unique<PositionComponent>(1.0, 2.0));
    world->Update();
    REQUIRE(system->updateCount == 1);

    const size_t entities = world->Entities.size();
    const size_t systems = world->Systems.size();
    const size_t componentTypes = world->Components.size();

    for(int i = 0; i < 3; i++)
    {
        world->Export();
        entity->Export();
    }

    REQUIRE(world->Entities.size() == entities);
    REQUIRE(world->Systems.size() == systems);
    REQUIRE(world->Components.size() == componentTypes);
    REQUIRE(system->updateCount == 1);
    auto after = entity->ComponentGet<PositionComponent>("Position");
    REQUIRE(after != nullptr);
    REQUIRE(after == std::dynamic_pointer_cast<PositionComponent>(position));
    REQUIRE(after->x == 1.0);
    REQUIRE(after->y == 2.0);
}

TEST_CASE("A system's update count shows in the export after updates", "[Export]") {
    ecs::Manager manager;
    auto world = manager.Container("world");
    world->System(counterMake("alpha", "first"));

    world->Update();
    world->Update();
    world->Update();

    REQUIRE(world->Export()["Systems"]["alpha"]["updateCount"] == 3);
}

TEST_CASE("A destroyed component and a destroyed entity leave the export", "[Export]") {
    ecs::Manager manager;
    auto world = manager.Container("world");
    auto entity = world->Entity("e");
    entity->Component(std::make_unique<PositionComponent>(1.0, 2.0));
    world->Entity("gone")->Component(std::make_unique<TagComponent>());

    entity->ComponentDestroy("Position");
    world->EntityDestroy("gone");

    const nlohmann::json expected = {{"Handle", "world"}, {"Entities", {{"e", {{"Handle", "e"}}}}}};
    REQUIRE(world->Export() == expected);
}
