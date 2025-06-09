#pragma once

#include <mega/file_service/file_callbacks.h>
#include <mega/file_service/file_context_pointer.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_range_forward.h>
#include <mega/file_service/file_range_vector.h>
#include <mega/file_service/file_service_context_badge_forward.h>

namespace mega
{
namespace file_service
{

class File
{
    FileContextPtr mContext;

public:
    File(FileServiceContextBadge badge, FileContextPtr context);

    File(const File& other) = default;

    File(File&& other);

    ~File();

    File& operator=(const File& rhs) = default;

    File& operator=(File&& rhs);

    // Retrieve information about this file.
    FileInfo info() const;

    // What ranges of this file are currently in storage?
    FileRangeVector ranges() const;

    // Read data from this file.
    void read(FileReadCallback callback, std::uint64_t offset, std::uint64_t length);

    void read(FileReadCallback callback, const FileRange& range);

    // Let the service know you want it to keep this file in storage.
    void ref();

    // Truncate this file to a specified size.
    void truncate(FileTruncateCallback callback, std::uint64_t size);

    // Let the service know you're happy for it to remove this file.
    void unref();

    // Write data to this file.
    void write(const void* buffer,
               FileWriteCallback callback,
               std::uint64_t offset,
               std::uint64_t length);

    void write(const void* buffer, FileWriteCallback callback, const FileRange& range);
}; // File

} // file_service
} // mega
