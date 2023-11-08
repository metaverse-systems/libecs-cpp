#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <libecs-cpp/ecs.hpp>

class TestSystem : public ecs::System
{
  public:
    TestSystem()
    {
        this->Handle = "TestSystem";
    }

    nlohmann::json Export()
    {
        nlohmann::json config;
        return config;
    }
    
};

TEST_CASE("Manager starts with no containers", "[Manager]") {
    ecs::Manager manager;
    REQUIRE(manager.ContainersGet().empty());
}

TEST_CASE("Manager can create and retrieve containers", "[Manager]") {
    ecs::Manager manager;
    auto container = manager.Container();
    REQUIRE(container != nullptr);
    REQUIRE_FALSE(manager.ContainersGet().empty());
}

TEST_CASE("Manager is running after construction", "[Manager]") {
    ecs::Manager manager;
    REQUIRE(manager.IsRunning());
}

TEST_CASE("Manager can shut down", "[Manager]") {
    ecs::Manager manager;
    manager.Shutdown();
    REQUIRE_FALSE(manager.IsRunning());
}

TEST_CASE("Manager can submit messages", "[Manager]") {
    ecs::Manager manager;
    std::string container_name = "Container 1";
    manager.Container(container_name)->System(new TestSystem());
    nlohmann::json message;
    message["destination"]["container"] = container_name;
    message["destination"]["system"] = "TestSystem";
    REQUIRE_NOTHROW(manager.MessageSubmit(message));
}