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
        virtual void Initialize();
        virtual void Configure(nlohmann::json config);
        virtual void Update() {};
        std::string Handle;
        ecs::Container *Container = nullptr;
        void MessageSubmit(nlohmann::json);
        virtual nlohmann::json Export() = 0;
        ecs::TypeEntityComponentList *Components = nullptr;
      protected:
        std::queue<nlohmann::json> messages;
        std::chrono::steady_clock::time_point LastTime = std::chrono::steady_clock::now();
        uint32_t DeltaTimeGet();
    };
}
