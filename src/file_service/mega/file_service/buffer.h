#pragma once

#include <mega/file_service/buffer_pointer.h>
#include <mega/file_service/sink.h>
#include <mega/file_service/source.h>

namespace mega
{

struct FileAccess;

namespace file_service
{

class Buffer: public Sink, public Source
{
protected:
    Buffer() = default;

public:
    virtual ~Buffer() = default;

    // Create a buffer.
    static BufferPtr create(FileAccess& file, std::uint64_t offset, std::uint64_t length);

    // Transfer data from this buffer into another buffer.
    virtual bool transfer(Buffer& target,
                          std::uint64_t offset0,
                          std::uint64_t offset1,
                          std::uint64_t length) const = 0;
}; // Buffer

} // file_service
} // mega
