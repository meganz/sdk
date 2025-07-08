#pragma once

#include <mega/file_service/file_event_forward.h>
#include <mega/file_service/file_range.h>

#include <cstdint>
#include <optional>

namespace mega
{
namespace file_service
{

struct FileEvent
{
    // What range, if any, has been modified?
    std::optional<FileRange> mRange;

    // What is the file's current modification time?
    std::int64_t mModified;

    // What is the file's current size?
    std::uint64_t mSize;
}; // FileEvent

} // file_service
} // mega
