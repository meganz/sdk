#pragma once

#include <mega/file_service/buffer.h>
#include <mega/file_service/file_buffer_forward.h>

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

    // Truncate the file's size.
    virtual bool truncate(std::uint64_t size);
}; // FileBuffer

} // file_service
} // mega
