#pragma once
#include <string>

#include <libecs-cpp/uuid_v4.h>

namespace ecs
{
    class Uuid
    {
      public:
        Uuid();
        Uuid(UUIDv4::UUID);
        Uuid(std::string);
        std::string Get();
      private:
        UUIDv4::UUID id;
    };
}
