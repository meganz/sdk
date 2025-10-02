#pragma once

#include <mega/file_service/file_id.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_truncate_event_forward.h>

#include <cstdint>
#include <optional>

namespace mega
{
namespace file_service
{

struct FileTruncateEvent
{
    bool operator==(const FileTruncateEvent& rhs) const
    {
        return mID == rhs.mID && mSize == rhs.mSize && mRange == rhs.mRange;
    }

    bool operator!=(const FileTruncateEvent& rhs) const
    {
        return !(*this == rhs);
    }

    // The portion of the file, if any, that has been "cut off."
    std::optional<FileRange> mRange;

    // The ID of the file that has been truncated.
    FileID mID;

    // The file's new size.
    std::uint64_t mSize;
}; // FileTruncateEvent

} // file_service
} // mega
