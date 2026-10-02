#include <catch2/catch_all.hpp>
#include <libecs-cpp/ecs.hpp>
#include <atomic>
#include <cstdlib>
#include <cstddef>
#include <memory>
#include <new>
#include <string>
#include <vector>

// Counting allocator: every replaceable form of global operator new counts
// only while the measuring flag is set. The flag is cleared before any
// REQUIRE so the test framework's own allocations are never counted.
namespace
{
    std::atomic<bool> measuring{false};
    std::atomic<std::size_t> allocationCount{0};

    inline void count() noexcept
    {
        if (measuring.load(std::memory_order_relaxed))
            allocationCount.fetch_add(1, std::memory_order_relaxed);
    }

    inline void *plainAlloc(std::size_t size)
    {
        void *p = std::malloc(size == 0 ? 1 : size);
        return p;
    }

    inline void *alignedAlloc(std::size_t size, std::size_t alignment)
    {
        if (size == 0)
            size = 1;
#ifdef _WIN32
        return _aligned_malloc(size, alignment);
#else
        std::size_t rounded = (size + alignment - 1) / alignment * alignment;
        return std::aligned_alloc(alignment, rounded);
#endif
    }

    inline void alignedFree(void *p) noexcept
    {
#ifdef _WIN32
        _aligned_free(p);
#else
        std::free(p);
#endif
    }
}

void *operator new(std::size_t size)
{
    count();
    if (void *p = plainAlloc(size))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t size)
{
    count();
    if (void *p = plainAlloc(size))
        return p;
    throw std::bad_alloc();
}
void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    count();
    return plainAlloc(size);
}
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
    count();
    return plainAlloc(size);
}
void *operator new(std::size_t size, std::align_val_t al)
{
    count();
    if (void *p = alignedAlloc(size, static_cast<std::size_t>(al)))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t size, std::align_val_t al)
{
    count();
    if (void *p = alignedAlloc(size, static_cast<std::size_t>(al)))
        return p;
    throw std::bad_alloc();
}
void *operator new(std::size_t size, std::align_val_t al, const std::nothrow_t &) noexcept
{
    count();
    return alignedAlloc(size, static_cast<std::size_t>(al));
}
void *operator new[](std::size_t size, std::align_val_t al, const std::nothrow_t &) noexcept
{
    count();
    return alignedAlloc(size, static_cast<std::size_t>(al));
}

void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete(void *p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void *p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void *p, std::align_val_t, const std::nothrow_t &) noexcept { alignedFree(p); }
void operator delete[](void *p, std::align_val_t, const std::nothrow_t &) noexcept { alignedFree(p); }

namespace
{
    class CountingSystem : public ecs::System
    {
      public:
        explicit CountingSystem(const std::string &handle) : ecs::System(handle)
        {
            this->Timing = ecs::Timing(0);
        }
        nlohmann::json Export() const override { return nlohmann::json::object(); }
        void Update() override { this->updates++; }
        int updates = 0;
        int timerFires = 0;
        void AddTimer()
        {
            int *fires = &this->timerFires;
            this->TimerAdd(ecs::Timer("tick", [fires]() { (*fires)++; }, 0, true));
        }
    };

    class RemovingSystem : public ecs::System
    {
      public:
        RemovingSystem(const std::string &handle, std::string victim)
          : ecs::System(handle), victim(std::move(victim))
        {
            this->Timing = ecs::Timing(0);
        }
        nlohmann::json Export() const override { return nlohmann::json::object(); }
        void Update() override
        {
            if (this->armed)
            {
                this->armed = false;
                this->Container->SystemDestroy(this->victim);
            }
        }
        bool armed = false;
        std::string victim;
    };

    std::string longHandle(int i)
    {
        return "update-allocation-system-handle-number-" + std::to_string(i);
    }

    // Builds a world of `count` systems whose first system removes the last
    // one when armed, runs a warm-up pass, then returns the allocation count
    // of one measured pass that performs the removal.
    std::size_t removalPassAllocations(int count)
    {
        ecs::Manager manager;
        auto container = manager.Container("allocation-removal");
        auto *first = static_cast<RemovingSystem *>(container->System(
          std::make_unique<RemovingSystem>(longHandle(0), longHandle(count - 1))));
        for (int i = 1; i < count; i++)
            container->System(std::make_unique<CountingSystem>(longHandle(i)));

        container->Update();

        first->armed = true;
        allocationCount = 0;
        measuring = true;
        container->Update();
        measuring = false;
        std::size_t result = allocationCount.load();

        REQUIRE_FALSE(container->Systems.contains(longHandle(count - 1)));
        return result;
    }
}

TEST_CASE("An update pass with no changes does not allocate", "[UpdateAllocation]")
{
    ecs::Manager manager;
    auto container = manager.Container("allocation-steady");
    std::vector<CountingSystem *> systems;
    for (int i = 0; i < 50; i++)
    {
        auto *s = static_cast<CountingSystem *>(
          container->System(std::make_unique<CountingSystem>(longHandle(i))));
        s->AddTimer();
        systems.push_back(s);
    }

    container->Update(); // warm-up

    allocationCount = 0;
    measuring = true;
    for (int i = 0; i < 100; i++)
        container->Update();
    measuring = false;
    std::size_t allocations = allocationCount.load();

    REQUIRE(allocations == 0);
    for (auto *s : systems)
    {
        REQUIRE(s->updates == 101);
        REQUIRE(s->timerFires == 101);
    }
}

TEST_CASE("An update pass with one removal allocates independently of system count", "[UpdateAllocation]")
{
    std::size_t small = removalPassAllocations(10);
    std::size_t large = removalPassAllocations(200);

    REQUIRE(small == large);
    REQUIRE(small <= 4);
}
