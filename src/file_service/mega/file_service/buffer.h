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

    // Copy data from this buffer to another.
    virtual auto copy(Buffer& target,
                      std::uint64_t sourceOffset,
                      std::uint64_t targetOffset,
                      std::uint64_t length) const -> std::pair<std::uint64_t, bool> = 0;

    // Check if this buffer is a file buffer.
    virtual bool isFileBuffer() const = 0;

    // Check if this buffer is a memory buffer.
    bool isMemoryBuffer() const;
}; // Buffer

} // file_service
} // mega
