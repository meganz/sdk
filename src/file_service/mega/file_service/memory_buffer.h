#pragma once

#include <mega/file_service/buffer.h>

#include <cstdint>
#include <memory>

namespace mega
{
namespace file_service
{

class MemoryBuffer: public Buffer
{
    std::shared_ptr<std::uint8_t[]> mBuffer;
    std::uint64_t mLength;

public:
    explicit MemoryBuffer(std::uint64_t length);

    // Copy data from this buffer to another.
    auto copy(Buffer& target,
              std::uint64_t offset0,
              std::uint64_t offset1,
              std::uint64_t length) const -> std::pair<std::uint64_t, bool> override;

    // Read data from the buffer.
    auto read(void* buffer, std::uint64_t offset, std::uint64_t length) const
        -> std::pair<std::uint64_t, bool> override;

    // Write data into the buffer.
    auto write(const void* buffer, std::uint64_t offset, std::uint64_t length)
        -> std::pair<std::uint64_t, bool> override;
}; // MemoryBuffer

} // file_service
} // mega
