# libecs-cpp
Entity Component System library for C++

````
#include <libecs-cpp/ecs.hpp>

class TestComponent : public ecs::Component
{
  public:
    uint64_t somedata;

    TestComponent(Json::value config)
    {
        this->Type = "TestComponent";
        this->somedata = config["somedata"].asUInt64();
    }
}

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
        ecs::ComponentMap Components = this->ComponentsGet();

        // ...and cycle through them
        for(auto &component : Components["TestComponent"])
        {
            // Cast to component class
            auto c = (TestComponent *)component.second;

            // Do stuff with component data
            c->somedata = c->somedata + 1;
        }
    }
}

int main(int argc, char *argv[])
{
    ecs::Container *world = ECS->Container();
    world->System(new TestSystem());

    ecs::Entity *e = world->Entity();
    e->Component(new TestComponent(3453521));

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

````
