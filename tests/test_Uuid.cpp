#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <latch>
#include <libecs-cpp/ecs.hpp>
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
