#pragma once

#include <cstdint>

namespace ecs
{
    typedef struct Resource
    {
        char *ptr;
        uint64_t size = 0;
    } Resource;
}
