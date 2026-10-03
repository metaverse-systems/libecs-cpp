#include <libecs-cpp/ecs.hpp>
#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <csignal>
#include <cstdlib>

class PositionComponent : public ecs::Component
{
  public:
    float x, y;

    PositionComponent(nlohmann::json config)
    {
        this->Type = "PositionComponent";
        this->x = config["x"].get<float>();
        this->y = config["y"].get<float>();
    }

    nlohmann::json Export() const
    {
        nlohmann::json config;
        config["x"] = this->x;
        config["y"] = this->y;
        return config;
    }

    ~PositionComponent() {}
};

class VelocityComponent : public ecs::Component
{
  public:
    float x, y;

    VelocityComponent(nlohmann::json config)
    {
        this->Type = "VelocityComponent";
        this->x = config["x"].get<float>();
        this->y = config["y"].get<float>();
    }

    nlohmann::json Export() const
    {
        nlohmann::json config;
        config["x"] = this->x;
        config["y"] = this->y;
        return config;
    }

    ~VelocityComponent() {}
};

namespace
{
    /* Set by Ctrl-C so the main loop can end and shut the worlds down cleanly */
    volatile std::sig_atomic_t interrupted = 0;

    void onInterrupt(int)
    {
        interrupted = 1;
    }
}

class PhysicsSystem : public ecs::System
{
  public:
    PhysicsSystem():
        System("PhysicsSystem")
    {
    }

    nlohmann::json Export() const
    {
        nlohmann::json config;
        config["Handle"] = this->Handle;
        return config;
    }

    /* Called once on the world's thread, before the first Update() */
    void Initialize()
    {
        std::cout << this->Handle << " started" << std::endl;
    }

    /* Called once on the world's thread when the system is removed or the world stops */
    void Shutdown()
    {
        std::cout << this->Handle << " shut down" << std::endl;
    }

    void Update()
    {
        /* Time that passed between the previous Update() and this one */
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(this->ElapsedGet());

        std::cout << "Last run " << dt.count() << "ms ago" << std::endl;

        // Type names held in std::string constants so lookups do not allocate
        static const std::string positionType = "PositionComponent";
        static const std::string velocityType = "VelocityComponent";

        // Look the type up with find(): indexing a missing type would insert an empty entry
        auto positions = this->Components->find(positionType);
        if(positions == this->Components->end()) return;

        // Iterate over PositionComponents
        for(auto &[entity, pcomponent] : positions->second)
        {
            // Cast to PositionComponent class
            auto pos = std::dynamic_pointer_cast<PositionComponent>(pcomponent);

            // Get the related VelocityComponent; the result is empty when the entity has none
            auto vel = this->Container->ComponentGet<VelocityComponent>(entity, velocityType);

            if(!pos || !vel)
            {
                std::cout << entity << " - has no velocity, skipped" << std::endl;
                continue;
            }

            // scale velocity
            float multiplier = dt.count() / 1000.0;

            // Adjust position data
            pos->x += vel->x * multiplier;
            pos->y += vel->y * multiplier;
            std::cout << entity << " - Position - x: " << pos->x << ", y: " << pos->y << "   Velocity - x: ";
            std::cout << vel->x << ", y: " << vel->y << "    Multiplier: " << multiplier << std::endl;
        }
    }
};

int main(int argc, char *argv[])
{
    /* Seconds to run, 0 means until interrupted */
    long seconds = 3;
    if(argc > 1) seconds = std::strtol(argv[1], nullptr, 10);
    if(seconds < 0) seconds = 0;

    std::signal(SIGINT, onInterrupt);

    auto world = ECS->Container();

    world->System(std::make_unique<PhysicsSystem>());

    /* Create a new entity in the 'world' container */
    auto e = world->Entity();

    /* Initialize a PositionComponent and add it to the Entity 'e' */
    nlohmann::json config;
    config["x"] = 1;
    config["y"] = 1;
    e->Component(std::make_unique<PositionComponent>(config));

    /* Initialize a VelocityComponent and add it to the Entity 'e' */
    config["x"] = 1; // meters per second
    config["y"] = 0;
    e->Component(std::make_unique<VelocityComponent>(config));

    /* An entity with a position and no velocity: the system skips it */
    auto still = world->Entity();
    config["x"] = 5;
    config["y"] = 5;
    still->Component(std::make_unique<PositionComponent>(config));

    /* Run container in its own thread */
    bool threaded = true;

    /*                   OR               */

    /* Run container loop in main thread, */
    /* OS X won't let GUI stuff happen    */
    /* outside the main thread)           */
#if __APPLE__
    threaded = false;
#endif

    /* Printed before the world runs so its output is not mixed with the world thread's */
    std::cout << world->Export() << std::endl;

    if(threaded) world->Start();
    else world->SystemsInitialize();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while(ECS->IsRunning() && !interrupted && (seconds == 0 || std::chrono::steady_clock::now() < deadline))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if(!threaded) world->Update();
    }

    /* Stops every threaded world and waits until they have ended and their systems were shut down */
    ECS->Shutdown();

    /* A world driven from this thread is stopped by its owner */
    if(!threaded) world->Stop();

    return 0;
}
