#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <libecs-cpp/ecs.hpp>

class TestComponent : public ecs::Component
{
  public:
    TestComponent()
    {
        this->Type = "TestComponent";
    }

    TestComponent(nlohmann::json config)
    {
        this->Type = "TestComponent";
        this->value = config["value"].get<uint64_t>();
    }

    nlohmann::json Export()
    {
        nlohmann::json config;
        config["value"] = this->value;
        return config;
    }

    uint64_t value = 0;
};

TEST_CASE("Entity is created with a unique handle", "[Entity]") {
    auto container = ECS->Container();
    auto entity = container->Entity();
    REQUIRE_FALSE(entity->Handle.empty());
}

TEST_CASE("Entity can be created with a custom handle", "[Entity]") {
    std::string customHandle = "myEntity";
    auto container = ECS->Container();
    auto entity = container->Entity(customHandle);
    REQUIRE(entity->Handle == customHandle);
}

TEST_CASE("Entity exports its data correctly", "[Entity]") {
    auto container = ECS->Container();
    auto entity = container->Entity();
    nlohmann::json exportedData = entity->Export();
    REQUIRE(exportedData["Handle"] == entity->Handle);
}

TEST_CASE("Entity can add and retrieve a component", "[Entity]") {
    auto container = ECS->Container();
    auto entity = container->Entity();
    nlohmann::json config;
    config["value"] = 5;
    //auto component = std::make_shared<TestComponent>(config);
    entity->Component(new TestComponent(config));
    auto retrievedComponent = container->Components["TestComponent"][entity->Handle];
    REQUIRE(retrievedComponent != nullptr);
}

TEST_CASE("Entity can destroy a component", "[Entity]") {
    auto container = ECS->Container();
    auto entity = container->Entity();
    nlohmann::json config;
    config["value"] = 5;
    entity->Component(new TestComponent(config));
    entity->ComponentDestroy("TestComponent");
    auto retrievedComponent = container->Components["TestComponent"][entity->Handle];
    REQUIRE(retrievedComponent == nullptr);
}

TEST_CASE("Entity can be destroyed", "[Entity]") {
    auto container = ECS->Container();
    auto entity = container->Entity();
    std::string handle = entity->Handle;
    entity->Destroy();
    // Assuming there is a way to check if an entity has been destroyed, e.g.:
    REQUIRE(container->Entities[handle] == nullptr);
}
