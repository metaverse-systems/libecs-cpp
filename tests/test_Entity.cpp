#include <catch2/catch_all.hpp>
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

    nlohmann::json Export() const
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
    entity->Component(std::make_unique<TestComponent>(config));
    auto retrievedComponent = container->ComponentGet(entity->Handle, "TestComponent");
    REQUIRE(retrievedComponent != nullptr);
}

TEST_CASE("Entity can destroy a component", "[Entity]") {
    auto container = ECS->Container();
    auto entity = container->Entity();
    nlohmann::json config;
    config["value"] = 5;
    entity->Component(std::make_unique<TestComponent>(config));
    // The component is there before it is destroyed, so the null result below comes from the destroy.
    REQUIRE(container->ComponentGet(entity->Handle, "TestComponent") != nullptr);
    REQUIRE(entity->ComponentGet("TestComponent") != nullptr);

    entity->ComponentDestroy("TestComponent");
    REQUIRE(container->ComponentGet(entity->Handle, "TestComponent") == nullptr);
    REQUIRE(entity->ComponentGet("TestComponent") == nullptr);

    // Destroying it again changes nothing and does not throw.
    REQUIRE_NOTHROW(entity->ComponentDestroy("TestComponent"));
    REQUIRE(container->ComponentGet(entity->Handle, "TestComponent") == nullptr);
}

TEST_CASE("Entity can be destroyed", "[Entity]") {
    auto container = ECS->Container();
    auto entity = container->Entity();
    std::string handle = entity->Handle;
    entity->Destroy();
    REQUIRE_FALSE(container->Entities.contains(handle));
}

class OtherComponent : public ecs::Component
{
  public:
    OtherComponent()
    {
        this->Type = "OtherComponent";
    }

    nlohmann::json Export() const
    {
        return nlohmann::json::object();
    }
};

// The next three cases guard removal that must not depend on statement order.
// They may already pass on older code.
TEST_CASE("EntityDestroy accepts the handle stored on the entity itself", "[Entity]") {
    auto container = ECS->Container();
    auto entity = container->Entity("entity-one-handle-longer-than-thirty-two-characters");
    auto other = container->Entity("entity-two-handle-longer-than-thirty-two-characters");
    const std::string handle = entity->Handle;
    const std::string otherHandle = other->Handle;
    entity->Component(std::make_unique<TestComponent>());
    entity->Component(std::make_unique<OtherComponent>());
    other->Component(std::make_unique<TestComponent>());

    container->EntityDestroy(entity->Handle);

    REQUIRE_FALSE(container->Entities.contains(handle));
    REQUIRE_FALSE(container->ComponentHas(handle, "TestComponent"));
    REQUIRE_FALSE(container->ComponentHas(handle, "OtherComponent"));
    REQUIRE(container->Entities.contains(otherHandle));
    REQUIRE(container->ComponentHas(otherHandle, "TestComponent"));
}

TEST_CASE("Entity Destroy removes the entity and its components", "[Entity]") {
    auto container = ECS->Container();
    auto entity = container->Entity("entity-one-handle-longer-than-thirty-two-characters");
    auto other = container->Entity("entity-two-handle-longer-than-thirty-two-characters");
    const std::string handle = entity->Handle;
    const std::string otherHandle = other->Handle;
    entity->Component(std::make_unique<TestComponent>());
    entity->Component(std::make_unique<OtherComponent>());
    other->Component(std::make_unique<TestComponent>());

    entity->Destroy();
    // `entity` is not touched after this point.

    REQUIRE_FALSE(container->Entities.contains(handle));
    REQUIRE_FALSE(container->ComponentHas(handle, "TestComponent"));
    REQUIRE_FALSE(container->ComponentHas(handle, "OtherComponent"));
    REQUIRE(container->Entities.contains(otherHandle));
    REQUIRE(container->ComponentHas(otherHandle, "TestComponent"));
}

TEST_CASE("ComponentDestroy accepts identifiers stored on the component itself", "[Entity]") {
    auto container = ECS->Container();
    auto entity = container->Entity("entity-one-handle-longer-than-thirty-two-characters");
    const std::string handle = entity->Handle;

    // Keep only a raw pointer so the container's map is the sole owner.
    ecs::Component *c = entity->Component(std::make_unique<TestComponent>()).get();
    entity->Component(std::make_unique<OtherComponent>());
    REQUIRE(container->ComponentGet(handle, "TestComponent").get() == c);

    container->ComponentDestroy(c->EntityHandle, c->Type);

    REQUIRE_FALSE(container->ComponentHas(handle, "TestComponent"));
    REQUIRE(container->ComponentHas(handle, "OtherComponent"));
    REQUIRE(container->Entities.contains(handle));
}

TEST_CASE("Removing unregistered entities and components is a no-op", "[Entity]") {
    auto container = ECS->Container();
    auto entity = container->Entity("entity-one-handle-longer-than-thirty-two-characters");
    const std::string handle = entity->Handle;
    entity->Component(std::make_unique<TestComponent>());

    REQUIRE_NOTHROW(container->EntityDestroy(""));
    REQUIRE_NOTHROW(container->ComponentDestroy("unknown-entity-handle-longer-than-thirty-two", "TestComponent"));
    REQUIRE_NOTHROW(container->ComponentDestroy(handle, "UnknownComponentType"));
    REQUIRE_NOTHROW(container->ComponentDestroy("", ""));

    REQUIRE(container->Entities.contains(handle));
    REQUIRE(container->ComponentHas(handle, "TestComponent"));
}
