#include <libecs-cpp/ecs.hpp>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class PositionLike : public ecs::Component
{
  public:
    PositionLike()
    {
        this->Type = "PositionLike";
    }

    nlohmann::json Export() const override
    {
        return nlohmann::json::object();
    }

    float x = 1.0f;
};

class VelocityLike : public ecs::Component
{
  public:
    VelocityLike()
    {
        this->Type = "VelocityLike";
    }

    nlohmann::json Export() const override
    {
        return nlohmann::json::object();
    }

    float x = 2.0f;
};

namespace
{
    /*! The only place the benchmark attaches a component. */
    void attach(ecs::Entity *target, ecs::Component *component)
    {
        target->Component(component);
    }

    double perLookup(std::chrono::steady_clock::duration elapsed, std::size_t lookups)
    {
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        return static_cast<double>(ns) / static_cast<double>(lookups);
    }
}

int main()
{
    constexpr int ENTITIES = 100000;
    constexpr int ROUNDS = 10;

    ecs::Manager manager;
    auto *container = manager.Container("bench");

    std::vector<std::string> handles;
    handles.reserve(ENTITIES);
    for(int i = 0; i < ENTITIES; i++)
    {
        handles.push_back("entity-" + std::to_string(i));
        auto *entity = container->Entity(handles.back());
        attach(entity, new PositionLike());
        if(i % 2 == 0)
        {
            attach(entity, new VelocityLike());
        }
    }

    const std::string positionType = "PositionLike";
    const std::string velocityType = "VelocityLike";
    double checksum = 0;

    auto tableIdiom = [&]() {
        for(const auto &handle : handles)
        {
            auto type = container->Components.find(velocityType);
            if(type == container->Components.end())
            {
                continue;
            }
            auto entry = type->second.find(handle);
            if(entry == type->second.end())
            {
                continue;
            }
            auto velocity = std::dynamic_pointer_cast<VelocityLike>(entry->second);
            if(velocity)
            {
                checksum += velocity->x;
            }
        }
    };
    auto componentGet = [&]() {
        for(const auto &handle : handles)
        {
            auto velocity = container->ComponentGet<VelocityLike>(handle, velocityType);
            if(velocity)
            {
                checksum += velocity->x;
            }
        }
    };
    auto componentHas = [&]() {
        for(const auto &handle : handles)
        {
            if(container->ComponentHas(handle, velocityType))
            {
                checksum += 1;
            }
        }
    };

    auto measure = [&](auto &&body) {
        body(); // warm-up
        auto start = std::chrono::steady_clock::now();
        for(int round = 0; round < ROUNDS; round++)
        {
            body();
        }
        return perLookup(std::chrono::steady_clock::now() - start, static_cast<std::size_t>(ROUNDS) * handles.size());
    };

    double table = measure(tableIdiom);
    double get = measure(componentGet);
    double has = measure(componentHas);

    std::cout << "Checksum: " << checksum << std::endl;
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Table idiom: " << table << " ns" << std::endl;
    std::cout << "ComponentGet: " << get << " ns" << std::endl;
    std::cout << "ComponentHas: " << has << " ns" << std::endl;
    std::cout << "Per lookup: " << get << " ns" << std::endl;

    return 0;
}
