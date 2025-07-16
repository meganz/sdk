#pragma once

#include <mega/file_service/sink_forward.h>

#include <cstdint>

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
    virtual std::uint64_t write(const void* buffer, std::uint64_t offset, std::uint64_t length) = 0;
}; // Sink

} // file_service
} // mega
