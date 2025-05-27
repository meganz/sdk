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

    ~File();

    // Retrieve information about this file.
    FileInfo info() const;

    // Read data from this file.
    void read(FileReadCallback callback, std::uint64_t offset, std::uint64_t length);

    void read(FileReadCallback callback, const FileRange& range);

    // What ranges of this file are currently in storage?
    FileRangeVector ranges() const;
}; // File

} // file_service
} // mega
