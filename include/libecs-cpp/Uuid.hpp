#pragma once
#include <string>

#ifndef DISABLE_BUILTIN_UUID
  #include <libecs-cpp/uuid_v4.h>
  namespace ecs
  {
      class Uuid
      {
        public:
          Uuid();
          Uuid(std::string id);
          std::string Get();
        private:
          UUIDv4::UUID id;
      };
  }
#else
  #include <uuid/uuid.h>
  namespace ecs
  {
      class Uuid
      {
        public:
          Uuid();
          Uuid(std::string id);
          std::string Get();
        private:
          uuid_t id;
      };
  }
#endif
