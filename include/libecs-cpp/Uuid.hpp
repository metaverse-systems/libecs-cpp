#pragma once
#include <string>

#ifdef _WIN32
  #include <rpc.h>
#else
  #include <uuid/uuid.h>
#endif

namespace ecs
{
    class Uuid
    {
      public:
        Uuid();
        Uuid(std::string);
        std::string Get();
      private:
        std::string uuid;
    };
}
