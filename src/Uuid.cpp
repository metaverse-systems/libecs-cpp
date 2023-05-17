#include <libecs-cpp/Uuid.hpp>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Uuid::Uuid()
    {
        UUIDv4::UUIDGenerator<std::mt19937_64> uuidGenerator; 
        this->id = uuidGenerator.getUUID();
    }

    Uuid::Uuid(std::string id)
    {
        this->id = UUIDv4::UUID(id); 
    }

    Uuid::Uuid(UUIDv4::UUID id)
    {
        this->id = id;
    }

    std::string Uuid::Get()
    {
        return this->id.str();
    }
}
