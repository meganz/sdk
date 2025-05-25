#pragma once

#include <mega/file_service/buffer.h>
#include <mega/file_service/buffer_pointer.h>
#include <mega/file_service/displaced_buffer_forward.h>
#include <mega/file_service/displaced_buffer_pointer.h>

#include <cstdint>

namespace mega
{
namespace file_service
{

class DisplacedBuffer: public Buffer
{
    BufferPtr mBuffer;
    std::uint64_t mDisplacement;

public:
    DisplacedBuffer(BufferPtr buffer, std::uint64_t displacement);

    // What buffer are we dispacing?
    BufferPtr buffer() const;

    // Update our displacement.
    void displacement(std::uint64_t displacement);

    // What is our displacement?
    std::uint64_t displacement() const;

    // Read data from the buffer.
    bool read(void* buffer, std::uint64_t offset, std::uint64_t length) const override;

    // Write data into the buffer.
    bool write(const void* buffer, std::uint64_t offset, std::uint64_t length) override;

    // Transfer data from this buffer into another buffer.
    bool transfer(Buffer& target,
                  std::uint64_t offset0,
                  std::uint64_t offset1,
                  std::uint64_t length) const override;
}; // DisplacedBuffer

// Displace a buffer.
DisplacedBufferPtr displace(BufferPtr buffer, std::uint64_t displacement);

} // file_service
} // mega
