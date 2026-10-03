#include <libecs-cpp/ecs.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

class EmptySystem : public ecs::System
{
public:
    explicit EmptySystem(const std::string &handle)
      : ecs::System(handle)
    {
    }

    void Update() override {}

    nlohmann::json Export() const override
    {
        return nlohmann::json::object();
    }
};

int main()
{
    constexpr int SYSTEMS = 100;
    constexpr int WARMUP = 10000;
    constexpr int N = 1000000;

    ecs::Manager manager;
    auto container = manager.Container("bench");

    for(int i = 0; i < SYSTEMS; i++)
    {
        container->System(std::make_unique<EmptySystem>("system-" + std::to_string(i)));
    }

    for(int i = 0; i < WARMUP; i++)
    {
        container->Update();
    }

    auto start = std::chrono::steady_clock::now();

    for(int i = 0; i < N; i++)
    {
        container->Update();
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "Ran " << N << " passes over " << container->Systems.size() << " systems in " << elapsed / 1000 << " us" << std::endl;
    std::cout << "Throughput: " << (N * 1e9 / elapsed) << " passes/sec" << std::endl;
    std::cout << std::fixed << std::setprecision(1) << "Per pass: " << (static_cast<double>(elapsed) / N) << " ns" << std::endl;

    return 0;
}
