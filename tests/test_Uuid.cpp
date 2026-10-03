#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <latch>
#include <libecs-cpp/ecs.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
bool isCanonicalV4(const std::string &text)
{
    if (text.size() != 36)
    {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (c != '-')
            {
                return false;
            }
        }
        else if (i == 14)
        {
            if (c != '4')
            {
                return false;
            }
        }
        else if (i == 19)
        {
            if (c != '8' && c != '9' && c != 'a' && c != 'b')
            {
                return false;
            }
        }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            return false;
        }
    }
    return true;
}

// Returns the message of the std::runtime_error thrown when parsing text,
// or an empty string if nothing was thrown.
std::string parseError(const std::string &text)
{
    try
    {
        const ecs::Uuid id(text);
        return std::string();
    }
    catch (const std::runtime_error &error)
    {
        return error.what();
    }
}

// Checks that parsing text throws std::runtime_error, that the message
// contains the expected substring, and that it is short and on one line.
void requireRejected(const std::string &text, const std::string &expected)
{
    REQUIRE_THROWS_AS(ecs::Uuid(text), std::runtime_error);
    const std::string message = parseError(text);
    INFO(message);
    REQUIRE(message.find(expected) != std::string::npos);
    REQUIRE(message.size() < 256);
    REQUIRE(message.find('\n') == std::string::npos);
}
} // namespace

TEST_CASE("Uuid has a small fixed layout", "[Uuid]")
{
    REQUIRE(sizeof(ecs::Uuid) == 16);
    REQUIRE(alignof(ecs::Uuid) <= alignof(std::uint64_t));
}

// Guards behaviour that already holds; it must not regress.
TEST_CASE("Generated identifiers are canonical version 4", "[Uuid]")
{
    for (int i = 0; i < 10000; ++i)
    {
        const ecs::Uuid id;
        const std::string text = id.Get();
        INFO(text);
        REQUIRE(isCanonicalV4(text));
    }
}

TEST_CASE("Identifiers from many threads are unique", "[Uuid]")
{
    constexpr int threadCount = 8;
    constexpr int perThread = 125000;

    std::vector<std::vector<std::string>> results(threadCount);
    std::latch start(threadCount);
    std::vector<std::thread> threads;
    for (int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&results, &start, t]() {
            auto &mine = results[t];
            mine.reserve(perThread);
            start.arrive_and_wait();
            for (int i = 0; i < perThread; ++i)
            {
                mine.push_back(ecs::Uuid().Get());
            }
        });
    }
    for (auto &thread : threads)
    {
        thread.join();
    }

    std::vector<std::string> all;
    all.reserve(threadCount * perThread);
    for (auto &mine : results)
    {
        for (auto &text : mine)
        {
            REQUIRE(isCanonicalV4(text));
            all.push_back(std::move(text));
        }
    }
    std::sort(all.begin(), all.end());
    REQUIRE(all.size() == 1000000);
    REQUIRE(std::adjacent_find(all.begin(), all.end()) == all.end());
}

// Guards behaviour that already holds; it must not regress.
TEST_CASE("Threads started together get different first identifiers", "[Uuid]")
{
    for (int round = 0; round < 50; ++round)
    {
        std::latch start(2);
        std::string first;
        std::string second;
        std::thread a([&]() {
            start.arrive_and_wait();
            first = ecs::Uuid().Get();
        });
        std::thread b([&]() {
            start.arrive_and_wait();
            second = ecs::Uuid().Get();
        });
        a.join();
        b.join();
        REQUIRE(first != second);
    }
}

TEST_CASE("Worlds on separate threads create entities with distinct handles", "[Uuid]")
{
    constexpr int threadCount = 4;
    constexpr int perThread = 10000;

    ecs::Manager manager;
    std::vector<ecs::Container *> containers;
    for (int i = 0; i < threadCount; ++i)
    {
        containers.push_back(manager.Container());
    }

    std::vector<std::vector<std::string>> results(threadCount);
    std::latch start(threadCount);
    std::vector<std::thread> threads;
    for (int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&results, &start, &containers, t]() {
            auto &mine = results[t];
            mine.reserve(perThread);
            start.arrive_and_wait();
            for (int i = 0; i < perThread; ++i)
            {
                mine.push_back(containers[t]->Entity()->Handle);
            }
        });
    }
    for (auto &thread : threads)
    {
        thread.join();
    }

    std::vector<std::string> all;
    for (auto &mine : results)
    {
        for (auto &text : mine)
        {
            REQUIRE(isCanonicalV4(text));
            all.push_back(std::move(text));
        }
    }
    std::sort(all.begin(), all.end());
    REQUIRE(all.size() == 40000);
    REQUIRE(std::adjacent_find(all.begin(), all.end()) == all.end());
}

TEST_CASE("Parsing returns exactly the identifier given", "[Uuid]")
{
    SECTION("A lowercase identifier comes back unchanged")
    {
        const std::string text = "550e8400-e29b-41d4-a716-446655440000";
        REQUIRE(ecs::Uuid(text).Get() == text);
    }

    SECTION("Uppercase and mixed case come back in lowercase")
    {
        const std::string lower = "550e8400-e29b-41d4-a716-446655440000";
        REQUIRE(ecs::Uuid("550E8400-E29B-41D4-A716-446655440000").Get() == lower);
        REQUIRE(ecs::Uuid("550e8400-E29B-41d4-A716-446655440000").Get() == lower);
    }

    SECTION("The smallest and largest values round-trip")
    {
        const std::string zeros = "00000000-0000-0000-0000-000000000000";
        const std::string ones = "ffffffff-ffff-ffff-ffff-ffffffffffff";
        REQUIRE(ecs::Uuid(zeros).Get() == zeros);
        REQUIRE(ecs::Uuid(ones).Get() == ones);
        REQUIRE(ecs::Uuid("FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF").Get() == ones);
    }

    SECTION("An identifier that is not version 4 round-trips")
    {
        const std::string text = "6ba7b810-9dad-11d1-c0b4-00c04fd430c8";
        REQUIRE(ecs::Uuid(text).Get() == text);
    }

    SECTION("Generated identifiers round-trip")
    {
        for (int i = 0; i < 10000; ++i)
        {
            const ecs::Uuid id;
            const std::string text = id.Get();
            INFO(text);
            REQUIRE(ecs::Uuid(text).Get() == text);
        }
    }
}

TEST_CASE("Malformed identifier text is rejected", "[Uuid]")
{
    const std::string valid = "550e8400-e29b-41d4-a716-446655440000";

    SECTION("Empty text")
    {
        requireRejected("", "text is empty");
    }

    SECTION("One character too short")
    {
        requireRejected(valid.substr(0, 35), "35 characters long, expected 36");
    }

    SECTION("One character too long")
    {
        requireRejected(valid + "0", "37 characters long, expected 36");
    }

    SECTION("A bad digit in the last position")
    {
        requireRejected("550e8400-e29b-41d4-a716-44665544000g", "invalid character 'g' at position 36");
    }

    SECTION("A wrong separator")
    {
        requireRejected("550e8400xe29b-41d4-a716-446655440000", "expected '-' at position 9, found 'x'");
    }

    SECTION("Digits with the hyphens at the end")
    {
        requireRejected("550e8400e29b41d4a716446655440000----", "expected '-' at position 9");
    }

    SECTION("An embedded zero byte")
    {
        std::string text = valid;
        text[34] = '\0';
        REQUIRE(text.size() == 36);
        requireRejected(text, "byte 0x00 at position 35");
        REQUIRE(parseError(text).find("\\x00") != std::string::npos);
    }

    SECTION("Surrounding spaces are not trimmed")
    {
        requireRejected(" " + valid, "expected 36");
        requireRejected(valid + " ", "expected 36");
    }

    SECTION("Braces and a urn prefix are not accepted")
    {
        requireRejected("{" + valid + "}", "expected 36");
        requireRejected("urn:uuid:" + valid, "expected 36");
    }

    SECTION("Very long text")
    {
        const std::string text(4194304, 'a');
        requireRejected(text, "4194304 characters long");
        REQUIRE(parseError(text).find("(first 64 characters)") != std::string::npos);
    }

    SECTION("The caller can catch the error and carry on")
    {
        bool caught = false;
        try
        {
            const ecs::Uuid id("not an identifier");
        }
        catch (const std::exception &)
        {
            caught = true;
        }
        REQUIRE(caught);
        REQUIRE(isCanonicalV4(ecs::Uuid().Get()));
    }
}
