#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <libecs-cpp/uuid_v4.h>

// Simulate old behavior: new PRNG per UUID
std::string uuid_old_style()
{
    UUIDv4::UUIDGenerator<std::mt19937_64> gen;
    return gen.getUUID().str();
}

// Current behavior: static PRNG reused
std::string uuid_new_style()
{
    static UUIDv4::UUIDGenerator<std::mt19937_64> gen;
    return gen.getUUID().str();
}

int main()
{
    constexpr int N = 100000;

    // Warm up
    for(int i = 0; i < 100; i++) { uuid_new_style(); uuid_old_style(); }

    // Benchmark new style (static generator)
    auto start = std::chrono::steady_clock::now();
    for(int i = 0; i < N; i++) { uuid_new_style(); }
    auto end = std::chrono::steady_clock::now();
    auto new_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Benchmark old style (per-call generator)
    start = std::chrono::steady_clock::now();
    for(int i = 0; i < N; i++) { uuid_old_style(); }
    end = std::chrono::steady_clock::now();
    auto old_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "Old (per-call PRNG): " << old_us << " us (" << (N * 1000000.0 / old_us) << " UUIDs/sec)" << std::endl;
    std::cout << "New (static PRNG):   " << new_us << " us (" << (N * 1000000.0 / new_us) << " UUIDs/sec)" << std::endl;
    std::cout << "Speedup: " << (double)old_us / new_us << "x" << std::endl;

    return 0;
}
