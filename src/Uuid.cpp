#include <libecs-cpp/Uuid.hpp>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Uuid::Uuid()
    {
        this->uuid.resize(36);

#ifdef _WIN32
        UUID id;
        UuidCreate(&id);
        RPC_CSTR szUuid = NULL;
        if(UuidToString(&id, &szUuid) == RPC_S_OK)
        {
            this->uuid = (char*) szUuid;
            RpcStringFree(&szUuid);
        }
#else
        uuid_t id;
        uuid_generate(id);
        uuid_unparse(id, &this->uuid[0]);
#endif
    }

    Uuid::Uuid(std::string id)
    {
        this->uuid = id;
    }

    std::string Uuid::Get()
    {
        return this->uuid;
    }
}
