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

    TestSystem(std::string Handle)
    {
        this->Handle = Handle;
    }

    nlohmann::json Export()
    {
        nlohmann::json config;
        return config;
    }
    
};

TEST_CASE("System is initialized with a valid handle", "[System]") {
    TestSystem system;
    REQUIRE_FALSE(system.Handle.empty());
}

TEST_CASE("System can be initialized with a custom handle", "[System]") {
    std::string customHandle = "custom_handle";
    TestSystem system(customHandle);
    REQUIRE(system.Handle == customHandle);
}

TEST_CASE("System starts with no messages", "[System]") {
    TestSystem system;
    REQUIRE(system.MessagesWaiting() == 0);
}

TEST_CASE("System can submit messages", "[System]") {
    TestSystem system;
    nlohmann::json message = {{"key", "value"}};
    system.MessageSubmit(message);
    REQUIRE(system.MessagesWaiting() == 1);
}

TEST_CASE("System can calculate delta time", "[System]") {
    TestSystem system;
    // First call to initialize LastTime
    system.DeltaTimeGet(); 
    // Small sleep to simulate passage of time
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    uint32_t deltaTime = system.DeltaTimeGet();
    REQUIRE(deltaTime >= 10); // Check if at least 10ms have passed
}
