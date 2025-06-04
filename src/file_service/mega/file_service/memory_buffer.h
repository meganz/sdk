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

    // Read data from the buffer.
    bool read(void* buffer, std::uint64_t offset, std::uint64_t length) const override;

    // Write data into the buffer.
    bool write(const void* buffer, std::uint64_t offset, std::uint64_t length) override;

    // Copy data from this buffer to another.
    bool copy(Buffer& target,
              std::uint64_t offset0,
              std::uint64_t offset1,
              std::uint64_t length) const override;
}; // MemoryBuffer

} // file_service
} // mega
