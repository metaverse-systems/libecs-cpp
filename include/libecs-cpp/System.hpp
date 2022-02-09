#pragma once

#include <string>
#include <queue>

namespace ecs
{
    class Container;

    class System
    {
      public:
        System();
        System(std::string Handle);
        virtual void Init();
        virtual void Configure(Json::Value config);
        virtual void Update() {};
        const std::string Handle;
        ecs::Container *Container = nullptr;
        void MessageSubmit(Json::Value);
        virtual Json::Value Export() = 0;
        ecs::TypeEntityComponentList *Components = nullptr;
      protected:
        std::queue<Json::Value> messages;
        std::chrono::steady_clock::time_point LastTime = std::chrono::steady_clock::now();
        uint32_t DeltaTimeGet();
    };
}
