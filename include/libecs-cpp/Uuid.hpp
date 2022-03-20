#pragma once
#include <string>

#include <libecs-cpp/uuid.h>

namespace ecs
{
    class Uuid
    {
      public:
        Uuid();
        Uuid(std::string);
        std::string Get();
      private:
        uuids::uuid id;
    };
}
