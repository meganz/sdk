#pragma once

#include <mega/file_service/sink_forward.h>

#include <cstdint>
#include <utility>

namespace mega
{
namespace file_service
{

class Sink
{
protected:
    Sink() = default;

public:
    virtual ~Sink() = default;

    // Write data into the sink.
    virtual auto write(const void* buffer, std::uint64_t offset, std::uint64_t length)
        -> std::pair<std::uint64_t, bool> = 0;
}; // Sink

} // file_service
} // mega
