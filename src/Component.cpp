#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Component::Component():
        Handle(ecs::Uuid().Get()) {}

    Component::Component(Json::Value config):
        Handle(ecs::Uuid().Get()) {}

    Component::~Component() {};
}
