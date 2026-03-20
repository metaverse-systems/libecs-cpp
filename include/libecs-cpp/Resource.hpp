#pragma once

#include <cstdint>
#include <vector>

namespace ecs
{
    struct Resource
    {
        std::vector<uint8_t> Data;
    };
}
