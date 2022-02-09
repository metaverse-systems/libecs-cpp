#include <libecs-cpp/ecs.hpp>
#include <iostream>

class PositionComponent : public ecs::Component
{
  public:
    float x, y;

    PositionComponent(Json::Value config)
    {
        this->Type = "PositionComponent";
        this->x = config["x"].asFloat();
        this->y = config["y"].asFloat();
    }

    Json::Value Export()
    {
        Json::Value config;
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

    VelocityComponent(Json::Value config)
    {
        this->Type = "VelocityComponent";
        this->x = config["x"].asFloat();
        this->y = config["y"].asFloat();
    }

    Json::Value Export()
    {
        Json::Value config;
        config["x"] = this->x;
        config["y"] = this->y;
        return config;
    }

    ~VelocityComponent() {}
};

class PhysicsSystem : public ecs::System
{
  public:
    PhysicsSystem():
        System("PhysicsSystem")
    {
    }

    Json::Value Export()
    {
        Json::Value config;
        config["Handle"] = this->Handle;
        return config;
    }

    void Update()
    {
        /* dt = number of milliseconds since last Update() */
        auto dt = this->DeltaTimeGet();

        std::cout << "Last run " << dt << "ms ago" << std::endl;

        // Iterate over PositionComponents
        for(auto &[entity, pcomponent] : (*this->Components)["PositionComponent"])
        {
            // Cast to PositionComponent class
            auto pos = std::dynamic_pointer_cast<PositionComponent>(pcomponent);

            // Get related Component
            auto vcomponent = (*this->Components)["VelocityComponent"][entity];
            // Cast to VelocityComponent
            auto vel = std::dynamic_pointer_cast<VelocityComponent>(vcomponent);
                
            // scale velocity
            float multiplier = dt / 1000.0;

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
    auto world = ECS->Container();

    world->System(new PhysicsSystem());

    /* Create a new entity in the 'world' container */
    auto e = world->Entity();

    /* Initialize a PositionComponent and add it to the Entity 'e' */
    Json::Value config;
    config["x"] = 1;
    config["y"] = 1;
    e->Component(new PositionComponent(config));

    /* Initialize a VelocityComponent and add it to the Entity 'e' */
    config["x"] = 1; // meters per second
    config["y"] = 0;
    e->Component(new VelocityComponent(config));

    /* Run container in its own thread */
    bool threaded = true;

    /*                   OR               */

    /* Run container loop in main thread, */
    /* OS X won't let GUI stuff happen    */
    /* outside the main thread)           */
#if __APPLE__
    threaded = false;
#endif

    if(threaded) world->Start();
    else world->SystemsInit();

    std::cout << world->Export() << std::endl;

    while(ECS->IsRunning())
    {
        usleep(100000);
        if(!threaded) world->Update();
    }

    return 0;
}
