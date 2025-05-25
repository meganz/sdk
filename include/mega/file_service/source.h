#pragma once

#include <mega/file_service/source_forward.h>

#include <cstdint>

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
    virtual bool read(void* buffer, std::uint64_t offset, std::uint64_t length) const = 0;
}; // Source

} // file_service
} // mega
