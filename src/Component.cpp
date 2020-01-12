#include <uuid/uuid.h>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Component::Component()
    {
        uuid_t uuid;
        this->Handle.resize(36);

#ifdef _WIN32
        UuidCreate(&uuid);
        RPC_CSTR szUuid = NULL;
        if(UuidToString(&uuid, &szUuid) == RPC_S_OK)
        {
            this->Handle = (char*) szUuid;
            RpcStringFree(&szUuid);
        }
#else
        uuid_generate(uuid);
        uuid_unparse(uuid, &this->Handle[0]);
#endif
    }

    Component::~Component() {};
}
