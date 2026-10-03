#include <libecs-cpp/ecs.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

class SinkSystem : public ecs::System
{
public:
    explicit SinkSystem(const std::string &handle)
      : ecs::System(handle)
    {
    }

    void Update() override
    {
        while(!this->messages.empty())
        {
            this->messages.pop();
            this->received++;
        }
    }

    size_t received = 0;

    nlohmann::json Export() const override
    {
        return nlohmann::json::object();
    }
};

int main()
{
    constexpr int WARMUP = 10000;
    constexpr int N = 1000000;
    constexpr int DRAIN_EVERY = 1000;

    ecs::Manager manager;
    auto container = manager.Container("world");
    auto sinkOwner = std::make_unique<SinkSystem>("sink");
    SinkSystem *sink = sinkOwner.get();
    container->System(std::move(sinkOwner));

    const nlohmann::json message = {
        {"destination", {{"container", "world"}, {"system", "sink"}}},
        {"sender", "bench"},
        {"number", 1},
        {"payload", "x"}};

    for(int i = 0; i < WARMUP; i++)
    {
        manager.MessageSubmit(message);
        if((i + 1) % DRAIN_EVERY == 0)
        {
            container->Update();
        }
    }
    container->Update();

    auto start = std::chrono::steady_clock::now();
    for(int i = 0; i < N; i++)
    {
        manager.MessageSubmit(message);
        if((i + 1) % DRAIN_EVERY == 0)
        {
            container->Update();
        }
    }
    auto end = std::chrono::steady_clock::now();
    container->Update();
    auto managerNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    start = std::chrono::steady_clock::now();
    for(int i = 0; i < N; i++)
    {
        container->MessageSubmit(message);
        if((i + 1) % DRAIN_EVERY == 0)
        {
            container->Update();
        }
    }
    end = std::chrono::steady_clock::now();
    auto worldNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Manager: " << (static_cast<double>(managerNs) / N) << " ns" << std::endl;
    std::cout << "World: " << (static_cast<double>(worldNs) / N) << " ns" << std::endl;
    std::cout << "Received: " << sink->received << ", waiting: " << sink->MessagesWaiting() << std::endl;
    std::cout << "Per message: " << (static_cast<double>(managerNs) / N) << " ns" << std::endl;

    return 0;
}
