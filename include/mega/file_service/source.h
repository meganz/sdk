#pragma once

#include <mega/file_service/source_forward.h>

#include <cstdint>
#include <utility>

namespace mega
{
namespace file_service
{

class Source
{
protected:
    Source() = default;

public:
    virtual ~Source() = default;

    // Read data from the source.
    virtual auto read(void* buffer, std::uint64_t offset, std::uint64_t length) const
        -> std::pair<std::uint64_t, bool> = 0;
}; // Source

} // file_service
} // mega
