#include <libecs-cpp/ecs.hpp>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>

int main()
{
    constexpr int N = 1000000;
    std::size_t sink = 0;

    auto report = [](const char *name, std::chrono::steady_clock::time_point start,
                     std::chrono::steady_clock::time_point end)
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        std::cout << name << ": " << (static_cast<double>(elapsed) / N) << " ns per operation" << std::endl;
    };

    // Warm-up
    for(int i = 0; i < N / 10; i++)
    {
        sink += ecs::Uuid().Get()[0];
    }

    auto start = std::chrono::steady_clock::now();
    for(int i = 0; i < N; i++)
    {
        ecs::Uuid id;
        sink += static_cast<std::size_t>(sizeof(id));
    }
    report("Uuid()", start, std::chrono::steady_clock::now());

    start = std::chrono::steady_clock::now();
    for(int i = 0; i < N; i++)
    {
        sink += ecs::Uuid().Get()[0];
    }
    report("Uuid().Get()", start, std::chrono::steady_clock::now());

    std::string text = ecs::Uuid().Get();
    start = std::chrono::steady_clock::now();
    for(int i = 0; i < N; i++)
    {
        sink += ecs::Uuid(text).Get()[0];
    }
    report("Uuid(text).Get()", start, std::chrono::steady_clock::now());

    std::cout << "Checksum: " << sink << std::endl;
    return 0;
}
