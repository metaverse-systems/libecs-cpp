#include <libecs-cpp/ecs.hpp>
#include <chrono>
#include <iostream>

int main()
{
    constexpr int N = 100000;

    ecs::Manager manager;
    auto container = manager.Container("bench");

    auto start = std::chrono::steady_clock::now();

    for(int i = 0; i < N; i++)
    {
        container->Entity();
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "Created " << N << " entities in " << elapsed << " us" << std::endl;
    std::cout << "Throughput: " << (N * 1000000.0 / elapsed) << " entities/sec" << std::endl;
    std::cout << "Per entity: " << (elapsed * 1000.0 / N) << " ns" << std::endl;

    return 0;
}
