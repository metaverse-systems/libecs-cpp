#pragma once

#include <cstdint>
#include <vector>

namespace ecs
{
    /*! A named block of bytes that a world stores with Container::ResourceAdd(). The world keeps the
     *  bytes in one shared, read-only object: Container::ResourceGet() returns a
     *  std::shared_ptr<const Resource>, empty for an unknown name, and every holder sees the same
     *  bytes without a copy. Pass the bytes to ResourceAdd() with std::move() to avoid copying them.
     *  Treat a resource as read-only once it has been added; adding another under the same name
     *  replaces it, and holders of the old one keep their data. */
    struct Resource
    {
        std::vector<uint8_t> Data;
    };
}
