#include <libecs-cpp/ecs.hpp>

#ifndef DISABLE_BUILTIN_UUID
ecs::Uuid::Uuid()
{
    static UUIDv4::UUIDGenerator<std::mt19937_64> uuidGenerator;
    this->id = uuidGenerator.getUUID();
}

ecs::Uuid::Uuid(const std::string &id)
{
    this->id = UUIDv4::UUID(id);
}

std::string ecs::Uuid::Get()
{
    return this->id.str();
}
#else
ecs::Uuid::Uuid()
{
    uuid_generate(this->id);
}

ecs::Uuid::Uuid(const std::string &id)
{
    uuid_parse(id.c_str(), this->id);
}

std::string ecs::Uuid::Get()
{
    char str_id[37] = {0};
    uuid_unparse(this->id, str_id); // 36 characters + 1 for null terminator
    return std::string(str_id);
}
#endif
