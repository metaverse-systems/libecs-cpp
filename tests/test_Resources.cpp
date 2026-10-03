#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Resources are stored per world and read back through the public API only.

namespace
{
    ecs::Resource resourceMake(std::vector<uint8_t> bytes)
    {
        ecs::Resource resource;
        resource.Data = std::move(bytes);
        return resource;
    }
}

TEST_CASE("A resource that is added is read back with identical bytes", "[Resources]") {
    ecs::Manager manager;
    auto world = manager.Container("resources");

    const std::vector<uint8_t> bytes = {0, 1, 2, 3, 250, 251, 255};
    world->ResourceAdd("blob", resourceMake(bytes));

    auto found = world->ResourceGet("blob");
    REQUIRE(found != nullptr);
    REQUIRE(found->Data == bytes);
}

TEST_CASE("A resource added from an lvalue is copied and the source is unchanged", "[Resources]") {
    ecs::Manager manager;
    auto world = manager.Container("resources");

    ecs::Resource source = resourceMake({9, 8, 7});
    world->ResourceAdd("copy", source);

    REQUIRE(source.Data == std::vector<uint8_t>({9, 8, 7}));
    auto found = world->ResourceGet("copy");
    REQUIRE(found != nullptr);
    REQUIRE(found->Data == std::vector<uint8_t>({9, 8, 7}));
}

TEST_CASE("Adding a resource under an existing name replaces it", "[Resources]") {
    ecs::Manager manager;
    auto world = manager.Container("resources");

    world->ResourceAdd("blob", resourceMake({1, 1, 1, 1}));
    auto old = world->ResourceGet("blob");
    REQUIRE(old != nullptr);

    world->ResourceAdd("blob", resourceMake({2, 2}));

    auto replaced = world->ResourceGet("blob");
    REQUIRE(replaced != nullptr);
    REQUIRE(replaced->Data == std::vector<uint8_t>({2, 2}));
    // The old bytes are no longer reachable by name; a holder of the old resource still has them.
    REQUIRE(replaced != old);
    REQUIRE(old->Data == std::vector<uint8_t>({1, 1, 1, 1}));
}

TEST_CASE("Resources with different names are independent", "[Resources]") {
    ecs::Manager manager;
    auto world = manager.Container("resources");

    world->ResourceAdd("a", resourceMake({1}));
    world->ResourceAdd("b", resourceMake({2}));
    world->ResourceAdd("a", resourceMake({3}));

    REQUIRE(world->ResourceGet("a")->Data == std::vector<uint8_t>({3}));
    REQUIRE(world->ResourceGet("b")->Data == std::vector<uint8_t>({2}));
}

TEST_CASE("A missing resource name returns an empty pointer and creates nothing", "[Resources]") {
    ecs::Manager manager;
    auto world = manager.Container("resources");

    // Current behaviour: an unknown name gives an empty pointer and does not throw.
    REQUIRE_NOTHROW(world->ResourceGet("missing"));
    REQUIRE(world->ResourceGet("missing") == nullptr);

    // Asking did not create an entry: adding afterwards and reading gives only the added bytes.
    world->ResourceAdd("missing", resourceMake({4}));
    REQUIRE(world->ResourceGet("missing")->Data == std::vector<uint8_t>({4}));

    // A name that was never added in another world is still missing.
    auto other = manager.Container("other");
    REQUIRE(other->ResourceGet("missing") == nullptr);
}

TEST_CASE("A resource moved in is stored with the original contents", "[Resources]") {
    ecs::Manager manager;
    auto world = manager.Container("resources");

    const std::vector<uint8_t> original(4096, 0x5a);
    ecs::Resource source = resourceMake(original);
    world->ResourceAdd("moved", std::move(source));

    auto found = world->ResourceGet("moved");
    REQUIRE(found != nullptr);
    REQUIRE(found->Data == original);
    // Only the stored contents are checked: after a move the source is valid but its contents are
    // not specified, so the test does not look at them.
}

TEST_CASE("A holder keeps its data after the resource is replaced and the world is destroyed", "[Resources]") {
    std::shared_ptr<const ecs::Resource> held;
    {
        ecs::Manager manager;
        auto world = manager.Container("resources");
        world->ResourceAdd("blob", resourceMake({6, 6, 6}));
        held = world->ResourceGet("blob");
        world->ResourceAdd("blob", resourceMake({7}));
    }
    REQUIRE(held != nullptr);
    REQUIRE(held->Data == std::vector<uint8_t>({6, 6, 6}));
}

TEST_CASE("An empty resource name is an ordinary name", "[Resources]") {
    ecs::Manager manager;
    auto world = manager.Container("resources");

    // Current behaviour: the empty string is accepted as a name, stored and read back like any other.
    REQUIRE_NOTHROW(world->ResourceAdd("", resourceMake({5, 5})));
    auto found = world->ResourceGet("");
    REQUIRE(found != nullptr);
    REQUIRE(found->Data == std::vector<uint8_t>({5, 5}));
    // It does not shadow other names.
    REQUIRE(world->ResourceGet("x") == nullptr);
}

TEST_CASE("A resource may hold no bytes", "[Resources]") {
    ecs::Manager manager;
    auto world = manager.Container("resources");

    world->ResourceAdd("empty", ecs::Resource{});
    auto found = world->ResourceGet("empty");
    REQUIRE(found != nullptr);
    REQUIRE(found->Data.empty());
}
