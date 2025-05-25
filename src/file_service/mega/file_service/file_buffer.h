#pragma once

#include <mega/file_service/buffer.h>

#include <cstdint>

namespace mega
{

struct FileAccess;

namespace file_service
{

class FileBuffer: public Buffer
{
    FileAccess& mFile;

public:
    explicit FileBuffer(FileAccess& file);

    // Read data from the buffer.
    bool read(void* buffer, std::uint64_t offset, std::uint64_t length) const override;

    // Write data into the buffer.
    bool write(const void* buffer, std::uint64_t offset, std::uint64_t length) override;

    // Transfer data from this buffer into another buffer.
    bool transfer(Buffer& target,
                  std::uint64_t offset0,
                  std::uint64_t offset1,
                  std::uint64_t length) const override;
}; // FileBuffer

} // file_service
} // mega
