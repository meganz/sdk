#pragma once

#include <mega/file_service/file_buffer.h>
#include <mega/file_service/file_info_context_forward.h>

namespace mega
{
namespace file_service
{

class SparseFileBuffer: public FileBuffer
{
    // Describes the file we're accessing.
    FileInfoContext& mInfo;

public:
    SparseFileBuffer(FileAccess& file, FileInfoContext& info);

    // Read data from the buffer.
    auto read(void* buffer, std::uint64_t offset, std::uint64_t length) const
        -> std::pair<std::uint64_t, bool> override;

    // Write data into the buffer.
    auto write(const void* buffer, std::uint64_t offset, std::uint64_t length)
        -> std::pair<std::uint64_t, bool> override;

    // Truncate the file's size.
    bool truncate(std::uint64_t size) override;
}; // SparseFileBuffer

} // file_service
} // mega
