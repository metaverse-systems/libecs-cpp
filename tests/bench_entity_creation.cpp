#include <libecs-cpp/ecs.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
    class Bench : public ecs::Component
    {
      public:
        Bench()
        {
            this->Type = "Bench";
        }

        nlohmann::json Export() const
        {
            return nlohmann::json::object();
        }
    };
}

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

    // Components are created standalone, so the time is only that of constructing them.
    std::vector<std::unique_ptr<Bench>> components;
    components.reserve(N);

    start = std::chrono::steady_clock::now();

    for(int i = 0; i < N; i++)
    {
        components.push_back(std::make_unique<Bench>());
    }

    end = std::chrono::steady_clock::now();
    auto componentNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "Created " << N << " components in " << (componentNs / 1000) << " us" << std::endl;
    std::cout << "Per component: " << (static_cast<double>(componentNs) / N) << " ns" << std::endl;

    return 0;
}
