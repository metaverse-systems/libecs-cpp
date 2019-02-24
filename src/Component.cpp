#include <uuid/uuid.h>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Component::Component()
    {
        uuid_t uuid;
        uuid_generate(uuid);

        this->Handle.resize(40);
        uuid_unparse(uuid, &this->Handle[0]);
    }
}
