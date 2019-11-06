#include <libecs-cpp/ecs.hpp>
#include <iostream>

class TestComponent : public ecs::Component
{
  public:
    uint64_t somedata;

    TestComponent(Json::Value config)
    {
        this->Type = "TestComponent";
        this->somedata = config["somedata"].asUInt64();
    }

    Json::Value save()
    {
        Json::Value config;
        config["somedata"] = (Json::Value::UInt64)this->somedata;
        return config;
    }
};

class TestSystem : public ecs::System
{
  public:
    TestSystem()
    {
        this->Handle = "TestSystem";
        this->ComponentRequest("TestComponent");
    }

    void Update(uint32_t dt)
    {
        /* dt = number of milliseconds since last Update() */

        // Get components specified with this->ComponentRequest()...
        auto Components = this->ComponentsGet();

        // ...and cycle through them
        for(auto &entity : Components["TestComponent"])
        {
            auto component_list = entity.second;
            while(auto component = component_list.Pop())
            {
                // Cast to TestComponent class
                auto c = std::dynamic_pointer_cast<TestComponent>(component);
                
                // Do stuff with component data
                c->somedata = c->somedata + 1;
                std::cout << entity.first << ": " << c->somedata << std::endl;
            }
        }
    }
};

int main(int argc, char *argv[])
{
    ecs::Container *world = ECS->Container();
    world->System(new TestSystem());

    ecs::Entity *e = world->Entity();
    Json::Value config;
    config["somedata"] = 3453521;
    e->Component(std::make_shared<TestComponent>(config));

    bool threaded = true;

    if(threaded)
    {
        /* Start a thread and run container loop */
        world->Start();
    }

    /*                   OR                  */

    /* Run container loop manually (MacOS    */
    /* won't let GUI stuff happen outside    */
    /* the main thread)                      */

    while(ECS->IsRunning())
    {
        usleep(10000);
        if(!threaded) world->Update();
    }

    return 0;
}
