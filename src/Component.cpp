#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Component::Component():
        Handle(ecs::Uuid().Get()) {}

    Component::Component(const nlohmann::json &/* config */):
        Handle(ecs::Uuid().Get()) {}
}
