#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Component::Component()
    {
        this->Handle = ecs::Uuid().Get();
    }

    Component::~Component() {};
}
