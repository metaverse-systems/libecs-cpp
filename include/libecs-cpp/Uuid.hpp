#pragma once
#include <string>
#include "../../config.h"

#ifdef USE_BUILTIN_UUID
  #include <libecs-cpp/uuid_v4.h>
  namespace ecs
  {
      class Uuid
      {
        public:
          Uuid()
          {
              UUIDv4::UUIDGenerator<std::mt19937_64> uuidGenerator;
              this->id = uuidGenerator.getUUID();
          }
          Uuid(std::string id)
          {
              this->id = UUIDv4::UUID(id);
          }
          std::string Get()
          {
              return this->id.str();
          }
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
          Uuid()
          {
              uuid_generate(this->id);
          }
          Uuid(std::string id)
          {
              uuid_parse(id.c_str(), this->id);
          }
          std::string Get()
          {
              char str_id[37] = {0};
              uuid_unparse(this->id, str_id);
              return std::string(str_id);
          }
        private:
          uuid_t id;
      };
  }
#endif
