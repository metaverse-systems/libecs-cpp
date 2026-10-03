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

// A component has no identifier of its own: it is addressed by its entity and type.
// The checks go through concepts so that a missing member makes the condition false instead of
// being a hard error in the requires-expression.
template <typename T>
concept HasHandle = requires(T &t) { t.Handle; };

template <typename T>
concept HasEntityHandleAndType = requires(T &t) { t.EntityHandle; t.Type; };

static_assert(!HasHandle<ecs::Component>, "Component must not carry a Handle");
static_assert(HasEntityHandleAndType<ecs::Component>, "Component keeps EntityHandle and Type");

namespace
{
    // Takes a configuration, passes it to the base and keeps nothing from it but what it reads itself.
    class ConfiguredComponent : public ecs::Component
    {
      public:
        ConfiguredComponent(const nlohmann::json &config)
            : ecs::Component(config)
        {
            this->Type = "Configured";
            this->label = config.value("label", std::string("none"));
        }

        nlohmann::json Export() const
        {
            return {{"label", this->label}};
        }

        std::string label;
    };

    // Does not read the configuration at all, whatever shape it has.
    class IgnoringComponent : public ecs::Component
    {
      public:
        IgnoringComponent(const nlohmann::json &config)
            : ecs::Component(config)
        {
            this->Type = "Ignoring";
        }

        nlohmann::json Export() const
        {
            return nlohmann::json::object();
        }
    };
}

TEST_CASE("A component built from a configuration keeps its type and its own export", "[Entity]") {
    auto container = ECS->Container();
    auto entity = container->Entity("e");

    auto component = entity->Component(std::make_unique<ConfiguredComponent>(nlohmann::json{{"label", "hello"}, {"extra", 7}}));

    REQUIRE(component->Type == "Configured");
    REQUIRE(component->EntityHandle == "e");
    REQUIRE(component->Export() == nlohmann::json({{"label", "hello"}}));
    REQUIRE(entity->Export()["Components"]["Configured"] == nlohmann::json({{"label", "hello"}}));
}

TEST_CASE("The component base does not read the configuration", "[Entity]") {
    for(const nlohmann::json &config : {nlohmann::json(nullptr), nlohmann::json::array({1, 2, 3}), nlohmann::json("text"),
                                         nlohmann::json{{"Handle", "from-config"}, {"Type", "from-config"}, {"EntityHandle", "from-config"}}})
    {
        IgnoringComponent component(config);
        REQUIRE(component.Type == "Ignoring");
        REQUIRE(component.EntityHandle.empty());
        REQUIRE(component.Export() == nlohmann::json::object());
    }
}

TEST_CASE("The exported content of a world with components is the expected JSON", "[Entity]") {
    ecs::Manager manager;
    auto world = manager.Container("world");
    auto first = world->Entity("first");
    first->Component(std::make_unique<ConfiguredComponent>(nlohmann::json{{"label", "a"}}));
    first->Component(std::make_unique<IgnoringComponent>(nlohmann::json::object()));
    world->Entity("second")->Component(std::make_unique<ConfiguredComponent>(nlohmann::json{{"label", "b"}}));

    const nlohmann::json expected = {
        {"Handle", "world"},
        {"Entities",
         {{"first",
           {{"Handle", "first"},
            {"Components", {{"Configured", {{"label", "a"}}}, {"Ignoring", nlohmann::json::object()}}}}},
          {"second", {{"Handle", "second"}, {"Components", {{"Configured", {{"label", "b"}}}}}}}}}};

    REQUIRE(world->Export() == expected);
}

TEST_CASE("The entity handle of a component is still set when it is attached", "[Entity]") {
    auto container = ECS->Container();
    auto entity = container->Entity("owner-entity");
    auto component = entity->Component(std::make_unique<TestComponent>());

    REQUIRE(component->EntityHandle == "owner-entity");
    REQUIRE(container->ComponentGet("owner-entity", "TestComponent") == component);
}
