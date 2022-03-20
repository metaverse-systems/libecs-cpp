#include <libecs-cpp/Uuid.hpp>
#include <libecs-cpp/ecs.hpp>

namespace ecs
{
    Uuid::Uuid()
    {
        std::random_device rd;
        auto seed_data = std::array<int, std::mt19937::state_size> {};
        std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
        std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
        std::mt19937 generator(seq);
        uuids::uuid_random_generator gen{generator};
        this->id = gen();
    }

    Uuid::Uuid(std::string id)
    {
        this->id = uuids::uuid::from_string(id).value();
    }

    std::string Uuid::Get()
    {
        return uuids::to_string(this->id);
    }
}
